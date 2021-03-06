## This is A ThreadPool written in C++11

### Basic Usage:
```
std::future<int> future;

future = ThreadPool::Instance().Execute([] (int i) -> int {
    sleep(1);       // simulate time consuming task
    return i * i;
}, 3);

printf("async func return %d.\n", future.get());
```

### Advance Usage1 (execute with serial tag):
```
// Define the serial_tag of the tasks which you want to execute serially
int taskA_serial_tag = 1;
int taskB_serial_tag = 2;

for (int i = 0; i < 4; ++i) {
    // passing the serial_tag as the first param
    ThreadPool::Instance().Execute(taskA_serial_tag, [=] {
        printf("task A%d running...\n", i);
        sleep(1);
        printf("task A%d done!\n", i);
    });

    ThreadPool::Instance().Execute(taskB_serial_tag, [=] {
        printf("task B%d running...\n", i);
        sleep(1);
        printf("task B%d done!\n", i);
    });
}
printf("main thread done.\n");
```
A possible output may be:
```
main thread done.
task A0 running...
task B0 running...
task A0 done!
task A1 running...
task B0 done!
task B1 running...
task A1 done!
task A2 running...
task B1 done!
task B2 running...
task A2 done!
task A3 running...
task B2 done!
task B3 running...
task A3 done!
task B3 done!
```
As you can see above, tasks with the same `serial_tag` execute serially.

This is useful for scenarios where you just want the tasks execute 
asynchronously from the main thread but not concurrently themselves.

### Advance Usage2 (execute after...):
```
ThreadPool::Instance().ExecuteAfter(2000, [=] {
    printf("task A running...\n");
    sleep(1);
    printf("task A done!\n");
});
ThreadPool::Instance().ExecuteAfter(1000, [=] {
    printf("task B running...\n");
    sleep(1);
    printf("task B done!\n");
});
printf("main thread done.\n");
```
The output may go as follows:
```
task B running...   # at 1th s
task B done!        # at 2th s
task A running...
task A done!        # at 3th s
```
or
```
task B running...   # at 1th s
task A running...   # at 2th s
task B done!
task A done!        # at 3th s
```

### Advance Usage3 (execute periodically):
```
ThreadPool::Instance().ExecutePeriodic(1000, [=] {
    printf("task running...\n");
});
```
