#pragma once
#include <assert.h>
#include <stdint.h>
#include <math.h>
#include <chrono>

#ifndef FORCE_INLINE
    #if defined(__GNUC__)
        #define FORCE_INLINE __attribute__((always_inline))
    #elif defined(_MSC_VER) && !defined(__clang__)
        #define FORCE_INLINE __forceinline
    #else
        #define FORCE_INLINE
    #endif
#endif

namespace microbench
{
    static int64_t clock_ns() noexcept {
        auto duration = std::chrono::high_resolution_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count(); 
    }

    template <typename Fn> 
    static int64_t ellapsed_time_ns(Fn fn) noexcept
    {
        int64_t from = clock_ns();
        fn();
        return clock_ns() - from;
    }
    
    struct Bench_Result
    {
        double mean_ms = 0.0;
        double deviation_ms = 0.0;
        double max_ms = 0.0;
        double min_ms = 0.0;
        
        //the number of runs coallesced into a single measurement
        // usually 1 but can be more for very small functions
        // all other stats in this struct are properly statistically
        // corrected for this behaviour
        int64_t batch_size = 0;
        //the number of times the measured function was run in total
        int64_t iters = 0; 
    };

    template <class Fn> static Bench_Result benchmark(int64_t max_time_ms, int64_t warm_up_ms, Fn measured_fn, int64_t runs_mult = 1, int64_t batch_of_clock_accuarcy_multiple = 5) noexcept;
    template <class Fn> static Bench_Result benchmark(int64_t max_time_ms, Fn measured_fn, int64_t runs_mult = 1, int64_t batch_of_clock_accuarcy_multiple = 5) noexcept;
    
    //Marks a pointer as used for the compiler
    static void use_pointer(char const volatile*) {}
    
    //Prevents the compiler from optimizing away the variable passed in
    template <typename T> FORCE_INLINE 
    static void do_no_optimize(T const& value);

    //Prevents the compiler from reordering read write instructions
    FORCE_INLINE 
    static void read_write_barrier();

    namespace time_consts
    {
        static constexpr int64_t SECOND_MILISECONDS  = 1'000;
        static constexpr int64_t SECOND_MIRCOSECONDS = 1'000'000;
        static constexpr int64_t SECOND_NANOSECONDS  = 1'000'000'000;
        static constexpr int64_t SECOND_PICOSECONDS  = 1'000'000'000'000;

        static constexpr int64_t MILISECOND_NANOSECONDS = SECOND_NANOSECONDS / SECOND_MILISECONDS;

        static constexpr int64_t MINUTE_SECONDS = 60;
        static constexpr int64_t HOUR_SECONDS = 60 * MINUTE_SECONDS;
        static constexpr int64_t DAY_SECONDS = 24 * HOUR_SECONDS;
        static constexpr int64_t WEEK_SECONDS = 7 * DAY_SECONDS;
    }
}

//Implementation
namespace microbench
{
    namespace benchmark_internal
    {
        struct Bench_Stats
        {
            int64_t batch_count = 0;
            int64_t batch_size = 0;

            //following all in ns:
            int64_t time_sum = 0; 
            int64_t squared_time_sum = 0;
            int64_t min_batch_time = 0;
            int64_t max_batch_time = 0;

            int64_t mean_time_estimate = 0;
        };

        template <typename Fn> 
        Bench_Stats gather_bench_stats(
            Fn measured_fn,
            int64_t max_time_ns, 
            int64_t warm_up_ns, 
            int64_t batch_time_ns, 
            int64_t min_batch_size = 1, 
            int64_t min_end_checks = 5) noexcept
        {
            assert(min_end_checks > 0);
            assert(min_batch_size > 0);
            assert(max_time_ns >= 0);
        
            if(batch_time_ns <= 0)
                batch_time_ns = 1;

            int64_t to_time = warm_up_ns;
            if(to_time > max_time_ns || to_time <= 0)
                to_time = max_time_ns;

            Bench_Stats stats;
            stats.batch_size = min_batch_size;
            stats.batch_count = 0;
            stats.time_sum = 0;
            stats.squared_time_sum = 0;
            stats.min_batch_time = (int64_t) 1 << 62; 
            //arbitrary very large number so that min will get overriden
            stats.max_batch_time = 0;
            stats.mean_time_estimate = 0;

            int64_t start = clock_ns();
            int64_t from = start;
            while(true)
            {
                int64_t reject = 0;
                for(int64_t i = 0; i < stats.batch_size; i++)
                    reject |= (int64_t) !measured_fn(); //we use binary ops to disable short circtuing

                int64_t now = clock_ns();
                int64_t batch_time = now - from;
                int64_t total_time = now - start;
                from = now;

                if(reject == false)
                {
                    //Instead of tracking the times themselves we track
                    // their deltas from mean time estimate
                    //This significantly improves the stability of subsequent deviation
                    // computation and all other statistics can be obtained by simply adding
                    // back the estimate.
                    //Also helps prevent overflows but that didnt ever happen anyways so it 
                    // largely doesnt matter
                    int64_t delta = batch_time - stats.mean_time_estimate;
                    stats.time_sum         += delta;
                    stats.squared_time_sum += delta * delta;
                    stats.batch_count      += 1;
            
                    if(stats.min_batch_time > delta)
                       stats.min_batch_time = delta;
               
                    if(stats.max_batch_time < delta)
                       stats.max_batch_time = delta;
                }

                if(total_time > to_time)
                {
                    //if is over break
                    if(total_time > max_time_ns)
                        break;

                    //else compute the batch size so that we will finish on time
                    // while checking at least min_end_checks times if we are finished
                    
                    //update the estimate using the acquired data
                    int64_t iters = stats.batch_count * stats.batch_size;
                    if(iters <= 0)
                        iters = 1;

                    stats.mean_time_estimate = stats.time_sum / iters;

                    int64_t remaining = max_time_ns - total_time;
                    int64_t num_checks = remaining / batch_time_ns;
                    if(num_checks < min_end_checks)
                        num_checks = min_end_checks;

                    //total_expected_iters = (iters / total_time) * remaining
                    //batch_size = total_expected_iters / num_checks
                    int64_t den = total_time * num_checks;
                    if(den <= 0) //prevent division by 0 or weird results
                        den = 1;

                    stats.batch_size = (iters * remaining) / den;
                    if(stats.batch_size < min_batch_size)
                        stats.batch_size = min_batch_size;

                    //discard stats so far
                    stats.batch_count = 0; 
                    stats.time_sum = 0;
                    stats.squared_time_sum = 0;
                    stats.min_batch_time = (int64_t) 1 << 62;
                    stats.max_batch_time = 0;
                    to_time = max_time_ns;
                }
            }
     
            return stats;
        }

        //converts the raw measured stats to meaningful statistics
        static Bench_Result process_stats(Bench_Stats stats, int64_t runs_mult)
        {
            using namespace microbench::time_consts;

            assert(stats.min_batch_time * stats.batch_count <= stats.time_sum && "min must be smaller than sum");
            assert(stats.max_batch_time * stats.batch_count >= stats.time_sum && "max must be bigger than sum");
        
            //runs_mult is in case we 'batch' our tested function: 
            // ie instead of running the tested function once we run it 100 times
            // this just means that the batch_size is multiplied runs_mult times
            int64_t batch_size = stats.batch_size * runs_mult;
            int64_t iters = batch_size * (stats.batch_count);
        
            double batch_deviation_ms = 0;
            if(stats.batch_count > 1)
            {
                double n = (double) stats.batch_count;
                double sum = (double) stats.time_sum;
                double sum2 = (double) stats.squared_time_sum;
            
                //Welford's algorithm for calculating varience
                double varience_ns = (sum2 - (sum * sum) / n) / (n - 1.0);

                //deviation = sqrt(varience) and deviation is unit dependent just like mean is
                batch_deviation_ms = sqrt(abs(varience_ns)) / (double) MILISECOND_NANOSECONDS;
            }

            double mean_ms = 0.0;
            double min_ms = 0.0;
            double max_ms = 0.0;

            int64_t adjusted_time_sum = stats.time_sum + stats.mean_time_estimate * stats.batch_count;
            int64_t adjutsted_min = stats.min_batch_time + stats.mean_time_estimate;
            int64_t adjutsted_max = stats.max_batch_time + stats.mean_time_estimate;

            assert(adjutsted_min * stats.batch_count <= adjusted_time_sum);
            assert(adjutsted_max * stats.batch_count >= adjusted_time_sum);
            if(iters != 0)
            {
                mean_ms = (double) adjusted_time_sum / (double) (iters * MILISECOND_NANOSECONDS);
                min_ms = (double) adjutsted_min / (double) (batch_size * MILISECOND_NANOSECONDS);
                max_ms = (double) adjutsted_max / (double) (batch_size * MILISECOND_NANOSECONDS);
            }

            assert(mean_ms >= 0 && min_ms >= 0 && max_ms >= 0);

            //We assume that summing all running times in a batch 
            // (and then dividing by its size = making an average)
            // is equivalent to picking random samples from the original distribution
            // => Central limit theorem applies which states:
            // deviation_sampling = deviation / sqrt(samples)
        
            // We use this to obtain the original deviation
            // => deviation = deviation_sampling * sqrt(samples)
        
            // but since we also need to take the average of each batch
            // to get the deviation of a single element we get:
            // deviation_element = deviation_sampling * sqrt(samples) / samples
            //                   = deviation_sampling / sqrt(samples)

            double sqrt_batch_size = sqrt((double) batch_size);
            if(batch_size == 0) //no division by 0
                sqrt_batch_size = 1.0;

            Bench_Result result;
            result.deviation_ms = batch_deviation_ms / sqrt_batch_size;

            //since min and max are also somewhere within the confidence interval
            // keeping the same confidence in them requires us to also apply the same correction
            // to the distance from the mean (this time * sqrt_batch_size because we already 
            // divided by batch_size when calculating min_ms)
            result.min_ms = mean_ms + (min_ms - mean_ms) * sqrt_batch_size; 
            result.max_ms = mean_ms + (max_ms - mean_ms) * sqrt_batch_size; 

            //the above correction can push min to be negative 
            // happens mostly with noop and generally is not a problem
            if(result.min_ms < 0.0)
                result.min_ms = 0.0;
            
            result.mean_ms = mean_ms; 
            result.batch_size = batch_size;
            result.iters = iters;

            //results must be plausible
            assert(result.iters >= 0);
            assert(result.batch_size >= 0);
            assert(result.min_ms >= 0.0);
            assert(result.max_ms >= 0.0);
            assert(result.mean_ms >= 0.0);
            assert(result.deviation_ms >= 0.0);
            assert(result.min_ms <= result.mean_ms && result.mean_ms <= result.max_ms);

            return result;
        }
    
        struct Clock_Stats
        {
            int64_t average = 0;
            int64_t min = 0; //usually 0
            int64_t max = 0; //usually equal to median
        };

        //We dont really care about the clocks accuracy as returned by the api. 
        //All we care about is the mean time between subsequent clock runs
        static Clock_Stats calculate_clock_stats(int64_t runs) noexcept
        {
            Clock_Stats stats = {};
            stats.max = ((int64_t) 1 << 62) * -1;
            stats.min = (int64_t) 1 << 62;

            int64_t sum = 0;
            for(int64_t i = 0; i < runs; i++)
            {
                volatile int64_t from = clock_ns();
                volatile int64_t to = clock_ns();
                volatile int64_t diff = to - from;
                sum += diff;

                if(diff < stats.min)
                    stats.min = diff;

                if(diff > stats.max)
                    stats.max = diff;
            }

            stats.average = sum / runs;
            if(stats.average == 0)
                stats.average ++;

            return stats;
        };
    }
    
    template <typename Fn> 
    Bench_Result benchmark(int64_t max_time_ms, int64_t warm_up_ms, Fn measured_fn, int64_t runs_mult, int64_t batch_of_clock_accuarcy_multiple) noexcept
    {
        using namespace benchmark_internal;
        (void) calculate_clock_stats(100); //warm up
        Clock_Stats clock_stats = calculate_clock_stats(1000);
        Bench_Stats stats = gather_bench_stats(measured_fn,
            max_time_ms * time_consts::MILISECOND_NANOSECONDS, 
            warm_up_ms * time_consts::MILISECOND_NANOSECONDS,
            batch_of_clock_accuarcy_multiple * clock_stats.average);

        return process_stats(stats, runs_mult);
    }
    
    template <typename Fn> 
    Bench_Result benchmark(int64_t max_time_ms, Fn measured_fn, int64_t runs_mult, int64_t batch_of_clock_accuarcy_multiple) noexcept
    {
        return benchmark(max_time_ms, max_time_ms / 20 + 1, measured_fn, runs_mult, batch_of_clock_accuarcy_multiple);
    }
    
    //modified version of DoNotOptimize and ClobberMemmory from google test
    #if (defined(__GNUC__) || defined(__clang__)) && !defined(__pnacl__) && !defined(EMSCRIPTN)
        #define HAS_INLINE_ASSEMBLY
    #endif

    #ifdef HAS_INLINE_ASSEMBLY
        template <typename T> FORCE_INLINE 
        static void do_no_optimize(T const& value)
        {
            asm volatile("" : : "r,m"(value) : "memory");
        }

        template <typename T> FORCE_INLINE 
        static void do_no_optimize(T& value)
        {
            #if defined(__clang__)
                asm volatile("" : "+r,m"(value) : : "memory");
            #else
                asm volatile("" : "+m,r"(value) : : "memory");
            #endif
        }

        FORCE_INLINE 
        static void read_write_barrier(){
            asm volatile("" : : : "memory");
        }
    #elif defined(_MSC_VER)
        template <typename T> FORCE_INLINE 
        static void do_no_optimize(T const& value) {
            use_pointer((char const volatile*) (void*) &value);
            _ReadWriteBarrier();
        }

        FORCE_INLINE 
        static void read_write_barrier() {
            _ReadWriteBarrier();
        }
    #else
        template <typename T> FORCE_INLINE 
        static void do_no_optimize(T const& value) {
            use_pointer((char const volatile*) (void*) &value);
        }

        FORCE_INLINE 
        static void read_write_barrier() {}
    #endif
}

