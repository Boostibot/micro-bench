# micro-bench
small and fast statistical micro benchmark

## Why
Most benchmark libraries are either full of smudge constanst and ad hoc reasoning or are extremely detailed and unwieldy. The goal of this benchmark is to be as simple as possible while staying properly based in statistics.

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
};

Bench_Result result = benchmark(1000 /* 1s in ms */, vector_push_back);
std::cout << "average time:       " << result.mean_ms << std::endl;
std::cout << "standard deviation: " << result.deviation_ms << std::endl;
```

The `Bench_Result` struct additionally holds other statistics. It is defined as:

```cpp
struct Bench_Result
{
    double mean_ms = 0.0;
    double deviation_ms = 0.0;
    double max_ms = 0.0;
    double min_ms = 0.0;

    int64_t batch_size = 0; //the number of runs coalescend into a single batch (see below for more info)
    int64_t iters = 0; //the total executions of the tested function
};
```
### Batch runs
Sometimes instead of trying to measure an entire batch of operations we are really only trying to measure one. To be more concrete so far we have measured the execution time of pushing 100 ints into a vector. If instead we would like to know the statistics about a single push back operation out of the 100 we can use the third argument to the `benchmark` function.
```cpp
Bench_Result result = benchmark(1000, vector_push_back, 100 /* 100 push back runs per function call*/);
```
This automatically recalculates and adjusts the statistics to reflect a single push back.

### Other settings

If we want more control over the benchmark we can use the second overload. We can specify the following
```cpp
Bench_Result result = benchmark(
    2000,   //max time in ms (including warump)
    100,    //warm up time in ms - the results obtained duing this time are discarted
    vector_push_back, //tested function
    100,    //batch size
    5       //how many clock accuracies worth of time to coalesce into a single run (see below for further explanaition)
    );
```
All of these are self explicatory except the final argument. To explain what it does we will first have to discuss a bit how the function handles functions that are too short - its execution time is below the clocks accuracy. In such cases we simply call the function enough times in a loop so that the resulting time is above the clock accuracy and we get a meaningful result. However where exactly is the clock accuracy is a difficult question. It is not a binary yes/no instead the closer we get to it the more unreliable the results will be. This is where this argument comes into play. It specifies the number of clock accuracies to take as *the* clock accuracy below which we start coalescing runs. By default this is set to 5 so that the coalescing only when absolutely necessary. For example on my machine with msvc clock implementation I get only 4 batch size while measuring noop - function that does nothing. Coalescing is properly accounetd for in the statistics (the same way batch size is - in fact we simply multiply the two together and use the reuslt). We even adjust the min and max to reflect this. So this constant can be set as high as one wants without affecting much but we get into a sort of phylosophical problem because we are no lonegr measuring the function we set out to measure to begin with! So in short this can be set as high as 100 if we only care about the average time (not affected by batch size at all). Such configuration will give the most stable and unbiased results but the higher this is set the more are the other statiscs just made up (made up but still soundly and correctly).

## Some of the more interesting notes

Before the program even starts we calculate the min, max, average and median times when measuring nothing using the clock implementation. We then use these stats to determine the clock accuracy which can then be used to correctly handle short functions. This is to my knowledge the only way to properly do this without diving into machine and os specific code and as such is absolutely portable on all platform supporting c++14 (minimum tested with).

We only call the function at one place in the benchmark function to maximize the likelyhood that the function will be properly inlined into the surrounding code which lest us skip the overhead of calling it. This also means that the warmup and the gathering faze of the function use the same codepath which sounds not ideal but we make good use of gathered statistics during the warmup phase. We estimate the average running time and track all statistics relative to it which greately increases the acuracy of the resulting standard deviation. We also use this data to properly coalesce too small functions. 
