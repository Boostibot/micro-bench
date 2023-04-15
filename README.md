# micro-bench
small statistical micro benchmark

## Why
Most benchmark libraries are either full of smudge constant and ad hoc reasoning or are extremely detailed and unwieldy. The goal of this benchmark is to be as simple as possible while staying properly based on statistics.

**Main properties include:**
- Single function, no allocations and exceptions
- No extra storage requirements
- No assumptions about hardware and accuracy of its clock
- Very tight measure loop with minimal overhead
- Proper handling of functions whose runtime is below the clock accuracy

## How to use
simply include the header file and call the `benchmark` function with the time for the benchmark and the measured function.

```cpp
using namespace microbench;

const auto vector_push_back = [&]{
    std::vector<int> vec;
    for(int i = 0; i < 100; i++)
        vec.push_back(i);

    do_no_optimize(vec); //make sure the result doesnt get optimized away
    return true;
};

Bench_Result result = benchmark(1000 /* 1s in ms */, vector_push_back);
std::cout << "average time:       " << result.mean_ms << "ms" << std::endl;
std::cout << "standard deviation: " << result.deviation_ms << "ms" << std::endl;
```

The `Bench_Result` struct additionally holds other statistics. It is defined as:

```cpp
struct Bench_Result
{
    double mean_ms = 0.0;
    double deviation_ms = 0.0;
    double max_ms = 0.0;
    double min_ms = 0.0;

    int64_t batch_size = 0; //the number of runs coalesced into a single batch (see below for more info)
    int64_t iters = 0; //the total executions of the tested function
};
```

### Reject option

With some operations there is a need to every once in a while do something that should not be a part of the measured code. This is what reject is for. To reject a run (or a whole batch in the case of batched run) return false from the measured function. 

```cpp
std::vector<int> vec;
const auto vector_pop_back = [&]{
    //when there is nothing to pop create a new vector
    //This allocates so we expect it to take much longer than pop!
    if(vec.size() == 0)
    {
        std::vector<int> copy(3000);
        vec = std::move(copy);
        return false; //reject this batch
    }

    vec.pop_back();

    do_no_optimize(vec);
    return true;
};

Bench_Result result = benchmark(1000, vector_pop_back);
std::cout << "average time:       " << result.mean_ms << "ms" << std::endl;
std::cout << "standard deviation: " << result.deviation_ms << "ms" << std::endl;
```

Note that sometimes the simple addition of the reject option will cause different results. This is because the compiler can no longer ignore the return value and has to include additional checks. To fully see the effects of rejecting a sample it is therefore useful to trick the compiler into generating the condtion branch on both reject and keep. This can be done for example by replacing `return false;` with either `return vec.size() == 0;` (which always equals false) for reject and `return vec.size() != 0;` for keep

This yields the following statistics on my machine:
```
--Keep--
average time:       5.65878e-06ms
standard deviation: 0.000123585ms
    
--Reject--
average time:       4.19732e-06ms
standard deviation: 5.39637e-05ms
```

While the difference on average time is small the standard deviation changed by a factor of 100x! Rejecting can make a big difference!

### Batch runs
Sometimes instead of trying to measure an entire batch of operations we are really only trying to measure one. To be more concrete so far we have measured the execution time of pushing 100 ints into a vector. If instead we would like to know the statistics about a single push back operation out of the 100 we can use the third argument to the `benchmark` function.
```cpp
Bench_Result result = benchmark(1000, vector_push_back, 100 /* 100 push back runs per function call*/);
```
This automatically recalculates and adjusts the statistics to reflect a single push back.

### Other settings

If we want more control over the benchmark we can use the second overload. We can specify the following:
```cpp
Bench_Result result = benchmark(
    2000,   //max time in ms (including warump)
    100,    //warm up time in ms - the results obtained duing this time are discarted
    vector_push_back, //tested function
    100,    //batch size
    5       //how many clock accuracies worth of time to coalesce into a single run (see below for further explanaition)
    );
```
All of these are self explanatory except the final argument. To explain what it does we will first have to discuss a bit how the function handles functions that are too short - its execution time is below the clock's accuracy. In such cases we simply call the function enough times in a loop so that the resulting time is above the clock accuracy and we get a meaningful result. However, where exactly is the clock accuracy is a difficult question. It is not a binary yes/no instead the closer we get to it the more unreliable the results will be. This is where this argument comes into play. It specifies the number of clock running time to take as the clock accuracy below which we start coalescing runs. By default this is set to 5 so that the coalescing only when absolutely necessary. For example on my machine with msvc clock implementation I get only 4 batch size while measuring noop - a function that does nothing. Coalescing is properly accounted for in the statistics (the same way batch size is - in fact we simply multiply the two together and use the result). We even adjust the min and max to reflect this. So this constant can be set as high as one wants without affecting much. We however get into a philosophical problem. We are no longer measuring the function we set out to measure! In short this can be set as high as 100 if we only care about the average time (not affected by batch size at all). Such configuration will give the most stable and unbiased results but the higher this is set the more are the other statistics made up (made up but still soundly and correctly). Can also be set to 0 to disable batching runs completely.

## Some of the more interesting notes

### On measuring short functions
We should always measure with the same settings across all benchmarks. Benchmarks are hardly ever "definitive" - "this is the right and only right runtime of this function" - for small functions. We can clearly see this while measuring against noop. If we disable batching by setting the last argument to zero we can get (depending on your machine, OS and other) strange results for example that a single push_back is in fact faster than noop! This is clearly nonsense and is caused by the fact that we used `batch_size = 100` for the vector measurement. If we use the same `batch_size` for the noop as well the results will again be plausible. But are they 'correct'? No! Whatever the ratio of push back iters to noop iters (or runtime opposite ratio but the result is the same) is we can always get it higher by increasing the last argument. If we set it to 10 we will get 2x the difference. If we set it to 100, 20x. If 1000 we get some absurd number around 3000x! This seems strange and seems to indicate that the benchmark is broken but it is absolutely logical if we think about it. The runtime of noop is 0 so whenever we measure we are only measuring the overhead of measuring. The amount of checks while measuring is proportional to the number of batches we can measure in that given time. But the number of batches is determined by the last argument! This means that as we increase the last argument the runtime goes to 0 and the ratio goes to infinity!

This effect is prominent on all short functions only the minimum measure time caps after we increase the last argument sufficiently instead of going to zero (theoretically the noop also caps because the for loop has a cost).


### Implementation
Before each benchmark run we calculate the clock average running time. Below some factor of this running time the time measurement is but a noise.In those cases we batch the function calls so that the total running time of the batch is above the specified factor * clock running time. This is to my knowledge the only way to properly handle this without diving into machine and OS specific code and as such is absolutely portable on all platforms supporting c++14 (minimum tested with).

We only call the function at one place in the benchmark function to maximise the likelihood that the function will be properly inlined into the surrounding code which lets us skip the overhead of calling it. This also means that the warmup and the gathering phase of the function use the same codepath which sounds not ideal but we make good use of gathered statistics during the warmup phase. We estimate the average running time and track all statistics relative to it which greatly increases the accuracy of the resulting standard deviation. We also use this data to properly coalesce too small functions.
