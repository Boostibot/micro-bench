#pragma once
#include <cassert>
#include <cstdint>
#include <cmath>
#include <chrono>

#define nodisc [[nodiscard]]
#define cast(...) (__VA_ARGS__)

namespace micro_bench
{
    //sorry c++ chrono but I really dont want to iteract with all those templates

    //Clock reporting time in nanoseconds
    nodisc 
    int64_t clock() noexcept {
        auto duration = std::chrono::high_resolution_clock::now().time_since_epoch();
        return duration_cast<std::chrono::nanoseconds>(duration).count(); 
    }

    //Ellapsed time in nanoseconds
    template <typename Fn> nodisc 
    int64_t ellapsed_time(Fn fn) noexcept
    {
        int64_t from = clock();
        fn();
        return clock() - from;
    }

    int64_t calculate_median_clock_accuarcy() noexcept
    {
        enum Runs
        {
            runs = 1000
        };
        int64_t times[runs] = {0};

        //gather samples
        for(int64_t i = 0; i < runs; i++)
        {
            int64_t from = clock();
            int64_t to = clock();
            times[i] = to - from;
        }
        
        //bubble sort
        for (int64_t i = 0; i < runs - 1; i++)
        {
            for (int64_t j = 0; j < runs - i - 1; j++)
                if (times[j] > times[j + 1])
                {
                    int64_t temp = times[j];
                    times[j] = times[j + 1];
                    times[j + 1] = temp;
                }
        }

        int64_t median = 0;
        if(runs % 2 == 0)
            median = (times[runs/2] + times[runs/2 + 1]) / 2;
        else
            median = times[runs/2];

        return median;
    }

    namespace time_consts
    {
        static const     int64_t CLOCK_ACCURACY = calculate_median_clock_accuarcy();

        //CLOCK_ACCURACY is measured by repeatedly calling clock() - clock()
        // as such the resulting CLOCK_ACCURACY is somewhere between
        // clock runtime and 2*clock runtime depending from which point in the clock
        // function is where the 'clock' actually 'starts'. We assume the maximum runtime
        // since it gives the best results on my pc when measuring noop but the 
        // choice is arbitrary
        static const     int64_t CLOCK_RUNTIME = CLOCK_ACCURACY / 2;

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

    template <typename Fn> nodisc 
    Bench_Stats gather_bench_stats(
        Fn tested_function,
        int64_t max_time_ns, 
        int64_t warm_up_ns, 
        int64_t batch_time_ns, 
        int64_t min_batch_size = 1, 
        int64_t min_end_checks = 5) noexcept
    {
        assert(batch_time_ns > 0);
        assert(min_end_checks > 0);
        assert(min_batch_size > 0);
        assert(max_time_ns >= 0);
        assert(warm_up_ns >= 0);
        
        int64_t to_time = warm_up_ns;
        if(to_time > max_time_ns || to_time <= 0)
            to_time = max_time_ns;

        Bench_Stats stats;
        stats.batch_size = min_batch_size;
        stats.batch_count = 0;
        stats.time_sum = 0;
        stats.squared_time_sum = 0;
        stats.min_batch_time = cast(int64_t) 1 << 63;
        stats.max_batch_time = 0;
        stats.mean_time_estimate = 0;

        int64_t start = clock();
        int64_t from = start;

        while(true)
        {
            for(int64_t i = 0; i < stats.batch_size; i++)
                tested_function();

            int64_t now = clock();
            int64_t batch_time = now - from;
            int64_t total_time = now - start;
            from = now;

            int64_t delta = batch_time - stats.mean_time_estimate;
            stats.time_sum         += delta;
            stats.squared_time_sum += delta * delta;
            stats.batch_count      += 1;
            
            if(stats.min_batch_time > batch_time)
               stats.min_batch_time = batch_time;
               
            if(stats.max_batch_time < batch_time)
               stats.max_batch_time = batch_time;

            if(total_time > to_time)
            {
                //if is over break
                if(total_time > max_time_ns)
                    break;
                    
                int64_t iters = stats.batch_count * stats.batch_size;
                stats.mean_time_estimate = stats.time_sum / iters;

                //else compute the batch size so that we will finish on time
                // while checking at least min_end_checks times if we are finished
                int64_t remaining = max_time_ns - total_time;
                int64_t num_checks = remaining / batch_time_ns;
                if(num_checks < min_end_checks)
                    num_checks = min_end_checks;

                //total_expected_iters = (iters / total_time) * remaining
                //batch_size = total_expected_iters / num_checks
                stats.batch_size = (iters * remaining) / (total_time * num_checks);
                if(stats.batch_size < min_batch_size)
                    stats.batch_size = min_batch_size;

                //discard stats so far
                stats.batch_count = 0; 
                stats.time_sum = 0;
                stats.squared_time_sum = 0;
                stats.min_batch_time = cast(int64_t) 1 << 63;
                stats.max_batch_time = 0;
                to_time = max_time_ns;
            }
        }
     
        return stats;
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
        int64_t batch_runs = 0;
    };

    Bench_Result process_stats(Bench_Stats const& stats, int64_t tested_function_calls_per_run = 1)
    {
        //tested_function_calls_per_run is in case we 'batch' our tested function: 
        // ie instead of running the tested function once we run it 100 times
        // this just means that the batch_size is multiplied tested_function_calls_per_run times
        using namespace time_consts;

        int64_t batch_runs = stats.batch_size * tested_function_calls_per_run;
        int64_t iters = batch_runs * (stats.batch_count);
        
        double batch_deviation_ms = 0;
        if(stats.batch_count > 1)
        {
            double n = cast(double) stats.batch_count;
            double sum = cast(double) stats.time_sum;
            double sum2 = cast(double) stats.squared_time_sum;
            
            //Welford's algorithm for calculating varience
            double varience = (sum2 - (sum * sum) / n) / (n - 1.0);

            //deviation = sqrt(varience) and deviation is unit dependent just like mean is
            batch_deviation_ms = sqrt(abs(varience)) / cast(double) MILISECOND_NANOSECONDS;
        }

        double mean_ms = 0.0;
        double min_ms = 0.0;
        double max_ms = 0.0;

        int64_t adjusted_time_sum = stats.time_sum + (stats.mean_time_estimate - CLOCK_RUNTIME) * stats.batch_count;
        int64_t adjutsted_min = stats.min_batch_time + stats.mean_time_estimate - CLOCK_RUNTIME;
        int64_t adjutsted_max = stats.max_batch_time + stats.mean_time_estimate - CLOCK_RUNTIME;
        if(iters != 0)
        {
            mean_ms = cast(double) adjusted_time_sum / cast(double) (iters * MILISECOND_NANOSECONDS);
            min_ms = cast(double) adjutsted_min / cast(double) MILISECOND_NANOSECONDS;
            max_ms = cast(double) adjutsted_max / cast(double) MILISECOND_NANOSECONDS;
        }

        //due to measure bias can all be sligtly negative for noop measuring
        // this happens very rarely and does not affect any other function
        // still we correct to positive
        mean_ms = abs(mean_ms);
        min_ms = abs(min_ms);
        max_ms = abs(max_ms);

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

        double sqrt_samples = sqrt(cast(double) batch_runs);

        Bench_Result result;
        result.deviation_ms = batch_deviation_ms / sqrt_samples;

        //since min and max are also somewhere within the confidence interval
        // keeping the same confidence in them requires us to also apply this correction
        result.min_ms = min_ms / sqrt_samples; 
        result.max_ms = max_ms / sqrt_samples; 
        result.mean_ms = mean_ms; 
        result.batch_runs = batch_runs;

        return result;
    }

    //benchmarks the given function
    template <typename Fn> nodisc 
    Bench_Result benchmark(int64_t max_time_ms, int64_t warm_up_ms, Fn tested_function, int64_t tested_function_calls_per_run = 1, int64_t batch_of_clock_accuarcy_multiple = 5) noexcept
    {
        Bench_Stats stats = gather_bench_stats(tested_function,
            max_time_ms * time_consts::MILISECOND_NANOSECONDS, 
            warm_up_ms * time_consts::MILISECOND_NANOSECONDS,
            batch_of_clock_accuarcy_multiple * time_consts::CLOCK_ACCURACY);

        return process_stats(stats, tested_function_calls_per_run);
    }
    
    //benchmarks the given function
    template <typename Fn> nodisc 
    Bench_Result benchmark(int64_t max_time_ms, Fn tested_function, int64_t tested_function_calls_per_run = 1) noexcept
    {
        return benchmark(max_time_ms, max_time_ms / 20 + 1, tested_function, tested_function_calls_per_run);
    }

    void use_pointer(char const volatile*) {}

    #ifndef FORCE_INLINE
        #if defined(__GNUC__)
            #define FORCE_INLINE __attribute__((always_inline))
        #elif defined(_MSC_VER) && !defined(__clang__)
            #define FORCE_INLINE __forceinline
        #else
            #define FORCE_INLINE
        #endif
    #endif
    
    //modified version of DoNotOptimize and ClobberMemmory from google test
    #if (defined(__GNUC__) || defined(__clang__)) && !defined(__pnacl__) && !defined(EMSCRIPTN)
        #define HAS_INLINE_ASSEMBLY
    #endif

    #ifdef HAS_INLINE_ASSEMBLY
        template <typename T> FORCE_INLINE 
        void do_no_optimize(T const& value)
        {
            asm volatile("" : : "r,m"(value) : "memory");
        }

        template <typename T> FORCE_INLINE 
        void do_no_optimize(T& value)
        {
            #if defined(__clang__)
                asm volatile("" : "+r,m"(value) : : "memory");
            #else
                asm volatile("" : "+m,r"(value) : : "memory");
            #endif
        }

        FORCE_INLINE 
        void read_write_barrier(){
            asm volatile("" : : : "memory");
        }
    #elif defined(_MSC_VER)
        template <typename T> FORCE_INLINE 
        void do_no_optimize(T const& value) {
            use_pointer(cast(char const volatile*) cast(void*) &value);
            _ReadWriteBarrier();
        }

        FORCE_INLINE 
        void read_write_barrier() {
            _ReadWriteBarrier();
        }
    #else
        template <typename T> FORCE_INLINE 
        void do_no_optimize(T const& value) {
            use_pointer(cast(char const volatile*) cast(void*) &value);
        }

        FORCE_INLINE 
        void read_write_barrier() {}
    #endif
}

#undef nodisc 
#undef cast
