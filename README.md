# b4
 4-bit opcode VM without jump addresses

```
Compile: cc b4.c -o b4
./b4.exe "'Hello, World!'.say"
```

Features:
1. succinct yet readable assembly syntax
2. tiny bytecode sizes.
3. arbitrarily long BCD integers.
4. functions.
5. fast.

The ideas is to replace the `jmp address` instruciton with four opcodes 
```
  < > [ ]
```

These are used for both looping, branching and jumping, allowing for high-level looking constructs like
```
  Value [ CodeIfTrue 0<] CodeIfFalse>  ; If/Else
  Count=?[?.top]                       ; Loop `Count` times, priting counter
```

The destinations these `[` and `]` are resolved when they are encountered.

As a result, instructions become smaller in size (we don't read immediate unless we need it). It also means we can easily stitch the chunks of compiled code, without doing relocation:
```
Expression = "Value [" + CodeIfTrue + "0<] CodeIfFalse>"`
```

To speed things a bit, we cache targets for the recently taken jumps.

If you want a computed jump, then just define a function.
```
1000:'A'.say:
1001:'B'.say:
1002:'C'.say:
computed_jump:1000+.:   ; add 1000 to the argument and call it
1.computed_jump         ; says B
```

see b4.c for more info.


Bytecode sizes of typical operations
```
  ?-                   ; negate (1 bytes)
  [??<]1>              ; not (4 bytes)
  not:[?@]1:           ; define `not` as a function (6 bytes)
  ?4=1[top.1+]         ; loop from 0 to 4, printing the count (8 bytes)
  1000000000=?[]       ; loop a billion times (8 bytes)
  %[.top=]             ; print 0 terminated string's bytes (5 bytes)
  1[1=]                ; endless loop (4 bytes)
  bool:[1@]?:          ; returns 1 if integer is not 0, or 0 (3 bytes)
  and:[[1@]?@]!?:      ; logical and (8 bytes)
  or:[!1@][1@]?:       ; logical or (9 bytes)
```
