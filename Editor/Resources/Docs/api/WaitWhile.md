# WaitWhile

`public class WaitWhile : YieldInstruction`

Waits while a condition is true. It is checked once a frame, starting with the frame after the wait begins.

## Methods

```
public WaitWhile(Func<bool> condition)
```

A wait that ends when `condition` returns false.

[All types](index)
