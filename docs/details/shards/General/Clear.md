`Clear` removes all the elements of the sequence that has been passed to it in the `:Name` parameter.

This shard works on both sequences and tables. Parameter `:Key` applies only to tables.

Since variables may be locally scoped (created with `(:Global false)`; exists only for current wire) or globally scoped (created with `(:Global true)`; exists for all wires of that mesh), both parameters `:Global` and `:Name` are used in combination to identify the correct variable to clear. 

Any input to this shard is ignored and instead passed through as its output.

!!! note "See also"
    - [`Drop`](../Drop)
    - [`DropFront`](../DropFront)
    - [`Erase`](../Erase)
    - [`Pop`](../Pop)
    - [`PopFront`](../PopFront)
    - [`Remove`](../Remove)
