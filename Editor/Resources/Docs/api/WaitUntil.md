# WaitUntil

`public class WaitUntil : YieldInstruction`

Waits until a condition becomes true. It is checked once a frame, starting with the frame after the wait begins.

## Methods

```
public WaitUntil(Func<bool> condition)
```

A wait that ends when `condition` returns true.

[All types](index)
