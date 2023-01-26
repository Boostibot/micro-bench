# micro-bench
small and fast statistical micro benchmark

## How the benchmark works
- Runs the measured function for warm up duration. 
- Calculates the number of batch runs (usually 1) of the measured function so that the total runing time is distinguishible from the running time of the benchmark procedure 
- Discards the measured statistics so far 
- Gathers new ones until max time is reached
- Used the 7 measured ints to calculate standard deviation and mean while properly adjusts for batch runs
