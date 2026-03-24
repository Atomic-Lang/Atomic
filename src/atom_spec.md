# Atom Language Specification v0.1

> A compiled, safe, general-purpose programming language.
> Clean syntax. Native performance. Memory safety without garbage collection.

---

## 1. Overview

**Atom** is a statically-typed, compiled programming language that combines Python's readability with Rust's memory safety and C's performance. It compiles directly to native x86-64 code (PE/COFF on Windows, ELF on Linux) with zero runtime overhead.

**Design Principles:**
- **Safe by default** — ownership system prevents use-after-free, double-free, and data races at compile time
- **Clean syntax** — indentation-based, minimal boilerplate, reads like pseudocode
- **Native performance** — compiles to machine code, no VM, no interpreter, no GC
- **C/C++ interop** — first-class FFI via library descriptors, call any C/C++ library directly
- **Batteries included** — rich standard library for systems, web, graphics, and general use

**File extension:** `.atom`

---

## 2. Lexical Structure

### 2.1 Encoding
Source files are UTF-8 encoded.

### 2.2 Indentation
Atom uses indentation to define blocks (like Python). The standard indent is 4 spaces. Tabs are not allowed.

```
if x > 0:
    print("positive")
    if x > 100:
        print("big")
```

### 2.3 Comments

```
# Single-line comment

##
    Multi-line comment.
    Everything indented under ## is part of the comment.
##
```

### 2.4 Keywords

```
let     mut     fn      return  if      else    elif
for     while   loop    break   continue
struct  enum    impl    trait   match
import  from    as      pub     mod
own     ref     move    copy    drop
true    false   nil     self    Self
type    defer   unsafe  extern  async   await
in      not     and     or      is
```

### 2.5 Operators

```
Arithmetic:     +  -  *  /  %  **
Bitwise:        &  |  ^  ~  <<  >>
Comparison:     ==  !=  <  >  <=  >=
Logical:        and  or  not
Assignment:     =  +=  -=  *=  /=  %=  **=  &=  |=  ^=  <<=  >>=
Access:         .  ::
Range:          ..  ..=
Arrow:          ->
Pipe:           |>
```

---

## 3. Type System

### 3.1 Primitive Types

| Type      | Size    | Description                    |
|-----------|---------|--------------------------------|
| `i8`      | 1 byte  | Signed 8-bit integer           |
| `i16`     | 2 bytes | Signed 16-bit integer          |
| `i32`     | 4 bytes | Signed 32-bit integer          |
| `i64`     | 8 bytes | Signed 64-bit integer          |
| `u8`      | 1 byte  | Unsigned 8-bit integer         |
| `u16`     | 2 bytes | Unsigned 16-bit integer        |
| `u32`     | 4 bytes | Unsigned 32-bit integer        |
| `u64`     | 8 bytes | Unsigned 64-bit integer        |
| `f32`     | 4 bytes | 32-bit floating point          |
| `f64`     | 8 bytes | 64-bit floating point          |
| `bool`    | 1 byte  | `true` or `false`              |
| `char`    | 4 bytes | Unicode scalar value (UTF-32)  |
| `str`     | varies  | UTF-8 string (owned)           |
| `void`    | 0 bytes | No value (function returns)    |

### 3.2 Type Inference

Atom uses bidirectional type inference. Types can be omitted when the compiler can deduce them.

```
let x = 42              # inferred as i32
let y = 3.14            # inferred as f64
let name = "Atom"       # inferred as str
let flag = true          # inferred as bool

let z: i64 = 42         # explicit type annotation
```

### 3.3 Type Aliases

```
type Byte = u8
type Point = (f64, f64)
type Result[T] = enum { Ok(T), Err(str) }
```

### 3.4 Generics

```
fn max[T: Ord](a: T, b: T) -> T:
    if a > b:
        return a
    return b

struct Stack[T]:
    items: Vec[T]

    fn push(mut self, item: T):
        self.items.append(item)

    fn pop(mut self) -> Option[T]:
        return self.items.pop()
```

### 3.5 Option and Result Types (built-in)

```
# Option - represents a value that may or may not exist
let value: Option[i32] = Some(42)
let empty: Option[i32] = None

# Result - represents success or failure
let ok: Result[i32, str] = Ok(42)
let err: Result[i32, str] = Err("something went wrong")

# Unwrapping with ? operator (propagates errors)
fn read_number(s: str) -> Result[i32, str]:
    let parsed = parse_int(s)?     # returns Err early if parsing fails
    return Ok(parsed * 2)
```

---

## 4. Variables and Mutability

### 4.1 Immutable by Default

```
let x = 10          # immutable
x = 20              # ERROR: cannot assign to immutable variable

let mut y = 10      # mutable
y = 20              # OK
```

### 4.2 Constants

```
const MAX_SIZE: i32 = 1024
const PI: f64 = 3.14159265358979
```

Constants are evaluated at compile time and inlined.

### 4.3 Shadowing

```
let x = 10
let x = x + 5       # shadows previous x, new binding
let x = "hello"     # can even change type with shadowing
```

---

## 5. Ownership System

Atom's ownership system ensures memory safety at compile time with zero runtime cost.

### 5.1 Ownership Rules

1. Every value has exactly **one owner** at a time
2. When the owner goes out of scope, the value is **dropped** (freed)
3. Ownership can be **moved** or **borrowed**

### 5.2 Move Semantics

```
let a = String("hello")
let b = a                # ownership MOVES from a to b
print(a)                 # ERROR: a has been moved

# Primitives (i32, f64, bool, etc.) are Copy by default
let x = 42
let y = x                # x is COPIED, both valid
print(x)                 # OK: 42
```

### 5.3 Borrowing (References)

```
let data = String("hello")

# Immutable borrow (multiple allowed)
let r1 = ref data        # immutable reference
let r2 = ref data        # OK: multiple immutable refs allowed
print(r1, r2)

# Mutable borrow (exclusive)
let mut text = String("hello")
let r3 = ref mut text    # mutable reference
r3.append(" world")      # OK: can modify through mut ref
# let r4 = ref text      # ERROR: cannot borrow while mut ref exists
```

### 5.4 Borrowing Rules

1. You can have **many immutable references** `ref T` OR **one mutable reference** `ref mut T` — never both
2. References must always be **valid** (no dangling pointers)
3. The borrow checker verifies these rules at compile time

### 5.5 Lifetimes (explicit when needed)

```
# Usually inferred, but can be explicit
fn longest['a](x: ref'a str, y: ref'a str) -> ref'a str:
    if x.len() > y.len():
        return x
    return y
```

### 5.6 Copy and Clone

```
# Types that implement Copy are duplicated automatically
# All primitives are Copy: i32, f64, bool, char, etc.

# Complex types must be explicitly cloned
let a = vec![1, 2, 3]
let b = a.clone()        # deep copy
let c = a                # MOVE (Vec is not Copy)
```

### 5.7 The `drop` Keyword

```
let data = load_big_file("huge.dat")
process(ref data)
drop data                # explicitly free memory early
# data is no longer accessible here
```

### 5.8 Defer

```
fn process_file(path: str) -> Result[str, Error]:
    let file = open(path)?
    defer file.close()    # runs when scope exits (success or error)

    let data = file.read_all()?
    return Ok(data)
```

---

## 6. Functions

### 6.1 Basic Functions

```
fn greet(name: str):
    print("Hello, {name}!")

fn add(a: i32, b: i32) -> i32:
    return a + b

# Single-expression functions (implicit return)
fn double(x: i32) -> i32: x * 2
```

### 6.2 Default Parameters

```
fn connect(host: str, port: i32 = 8080, timeout: f64 = 30.0):
    # ...

connect("localhost")
connect("localhost", 3000)
connect("localhost", timeout=5.0)    # named argument
```

### 6.3 Multiple Return Values

```
fn divmod(a: i32, b: i32) -> (i32, i32):
    return (a / b, a % b)

let (quotient, remainder) = divmod(17, 5)
```

### 6.4 Closures / Lambdas

```
let square = |x: i32| -> i32: x * x
let nums = [1, 2, 3, 4, 5]
let squared = nums.map(|x| x * x)

# Multi-line closures
let process = |data: str| -> str:
    let cleaned = data.trim()
    let upper = cleaned.to_upper()
    return upper
```

### 6.5 Pipe Operator

```
let result = "  hello world  "
    |> str.trim()
    |> str.split(" ")
    |> list.map(str.capitalize)
    |> str.join(" ")

# result = "Hello World"
```

### 6.6 Function Overloading (via traits)

```
trait Display:
    fn to_string(self) -> str

impl Display for i32:
    fn to_string(self) -> str:
        return int_to_str(self)

impl Display for f64:
    fn to_string(self) -> str:
        return float_to_str(self)
```

---

## 7. Control Flow

### 7.1 If / Elif / Else

```
if temperature > 30:
    print("hot")
elif temperature > 20:
    print("warm")
else:
    print("cold")

# If as expression
let status = if code == 200: "ok" else: "error"
```

### 7.2 Match (Pattern Matching)

```
match value:
    0:
        print("zero")
    1..=9:
        print("single digit")
    n if n < 0:
        print("negative: {n}")
    _:
        print("other")

# Match on enums
match result:
    Ok(value):
        print("got: {value}")
    Err(msg):
        print("error: {msg}")

# Match as expression
let label = match code:
    200: "ok"
    404: "not found"
    500: "server error"
    _: "unknown"
```

### 7.3 Loops

```
# For loop (iterating)
for item in collection:
    print(item)

for i in 0..10:
    print(i)         # 0 to 9

for i in 0..=10:
    print(i)         # 0 to 10

for (index, value) in list.enumerate():
    print("{index}: {value}")

# While loop
while condition:
    do_work()

# Infinite loop
loop:
    let input = read_line()
    if input == "quit":
        break
    process(input)

# Loop with break value
let result = loop:
    let data = try_fetch()
    if data.is_ok():
        break data.unwrap()
```

### 7.4 For Comprehensions

```
let squares = [x * x for x in 0..10]
let evens = [x for x in numbers if x % 2 == 0]
let pairs = [(x, y) for x in 0..5 for y in 0..5 if x != y]
```

---

## 8. Data Structures

### 8.1 Structs

```
struct Point:
    x: f64
    y: f64

let p = Point(x=1.0, y=2.0)
print(p.x)

# Struct with methods
struct Circle:
    center: Point
    radius: f64

impl Circle:
    fn new(x: f64, y: f64, r: f64) -> Circle:
        return Circle(center=Point(x, y), radius=r)

    fn area(self) -> f64:
        return PI * self.radius ** 2

    fn scale(mut self, factor: f64):
        self.radius *= factor

let c = Circle.new(0.0, 0.0, 5.0)
print(c.area())
```

### 8.2 Enums

```
enum Color:
    Red
    Green
    Blue
    Custom(u8, u8, u8)

let c = Color.Custom(255, 128, 0)

match c:
    Color.Red: print("red")
    Color.Custom(r, g, b): print("rgb({r},{g},{b})")
    _: print("other")

# Enum with methods
enum Direction:
    North
    South
    East
    West

impl Direction:
    fn opposite(self) -> Direction:
        match self:
            Direction.North: Direction.South
            Direction.South: Direction.North
            Direction.East: Direction.West
            Direction.West: Direction.East
```

### 8.3 Tuples

```
let point = (10, 20)
let (x, y) = point

let mixed = (42, "hello", true)
print(mixed.0)    # 42
print(mixed.1)    # "hello"
```

### 8.4 Arrays and Slices

```
# Fixed-size array (stack allocated)
let arr: [i32; 5] = [1, 2, 3, 4, 5]

# Slice (view into array or vec)
let slice = arr[1..4]    # [2, 3, 4]

# Dynamic array (heap allocated)
let mut nums = vec![1, 2, 3]
nums.push(4)
nums.pop()
```

### 8.5 Maps and Sets

```
let mut scores = map!{"alice": 100, "bob": 85}
scores["charlie"] = 92
scores.remove("bob")

for (name, score) in scores:
    print("{name}: {score}")

let mut tags = set!{"rust", "python", "atom"}
tags.add("go")
if "atom" in tags:
    print("found it!")
```

---

## 9. Traits (Interfaces)

```
trait Drawable:
    fn draw(self, x: i32, y: i32)
    fn bounds(self) -> (i32, i32, i32, i32)

    # Default implementation
    fn is_visible(self) -> bool:
        return true

trait Resizable:
    fn resize(mut self, w: i32, h: i32)

# Implement traits for types
impl Drawable for Circle:
    fn draw(self, x: i32, y: i32):
        # drawing logic...
        pass

    fn bounds(self) -> (i32, i32, i32, i32):
        let r = self.radius as i32
        return (self.center.x as i32 - r, self.center.y as i32 - r,
                self.center.x as i32 + r, self.center.y as i32 + r)

# Trait bounds
fn render[T: Drawable + Resizable](obj: T):
    obj.draw(0, 0)

# Trait objects (dynamic dispatch)
fn draw_all(objects: Vec[ref dyn Drawable]):
    for obj in objects:
        obj.draw(0, 0)
```

### 9.1 Built-in Traits

```
trait Copy          # type can be copied implicitly (bitwise)
trait Clone         # type can be deep-copied via .clone()
trait Drop          # custom destructor
trait Eq            # equality comparison (== !=)
trait Ord           # ordering comparison (< > <= >=)
trait Hash          # can be hashed (for maps/sets)
trait Display       # human-readable string representation
trait Debug         # debug string representation
trait Default       # has a default value
trait Iterator      # can be iterated
trait Into[T]       # can convert into T
trait From[T]       # can be constructed from T
```

### 9.2 Deriving Traits

```
#[derive(Eq, Hash, Clone, Debug, Display)]
struct User:
    name: str
    age: i32
```

---

## 10. Error Handling

### 10.1 Result Type

```
fn read_file(path: str) -> Result[str, IOError]:
    if not file_exists(path):
        return Err(IOError("file not found: {path}"))
    # ...
    return Ok(content)
```

### 10.2 The ? Operator

```
fn process_config() -> Result[Config, Error]:
    let text = read_file("config.atom")?       # propagates Err
    let parsed = parse_toml(text)?              # propagates Err
    let config = validate(parsed)?              # propagates Err
    return Ok(config)
```

### 10.3 Try/Catch (sugar over Result)

```
try:
    let file = open("data.txt")?
    let data = file.read_all()?
    process(data)
catch IOError as e:
    print("IO error: {e.message}")
catch ParseError as e:
    print("Parse error: {e.message}")
catch:
    print("unknown error")
```

### 10.4 Panic (unrecoverable)

```
fn get_item(index: i32) -> Item:
    if index < 0:
        panic("index cannot be negative")
    return items[index]
```

---

## 11. Strings

### 11.1 String Types

```
# str - owned, heap-allocated, UTF-8 string
let s: str = "hello world"

# ref str - borrowed string slice
fn greet(name: ref str):
    print("hello {name}")
```

### 11.2 String Interpolation

```
let name = "Atom"
let version = 1
print("Welcome to {name} v{version}")
print("2 + 2 = {2 + 2}")
print("upper: {name.to_upper()}")
```

### 11.3 Multi-line Strings

```
let query = """
    SELECT *
    FROM users
    WHERE active = true
    ORDER BY name
"""

# Raw strings (no escape processing)
let regex = r"\d+\.\d+"
let path = r"C:\Users\atom\projects"
```

### 11.4 String Methods

```
let s = "Hello, World!"
s.len()              # 13
s.is_empty()         # false
s.contains("World")  # true
s.starts_with("He")  # true
s.to_upper()         # "HELLO, WORLD!"
s.to_lower()         # "hello, world!"
s.trim()             # removes whitespace
s.split(", ")        # ["Hello", "World!"]
s.replace("World", "Atom")  # "Hello, Atom!"
s[0..5]              # "Hello"
```

---

## 12. Modules and Imports

### 12.1 Module System

```
# File: math/vector.atom
mod math.vector

pub struct Vec2:
    pub x: f64
    pub y: f64

pub fn dot(a: Vec2, b: Vec2) -> f64:
    return a.x * b.x + a.y * b.y

fn internal_helper():    # private (no pub)
    pass
```

### 12.2 Imports

```
import math.vector                         # import module
from math.vector import Vec2, dot          # import specific items
from math.vector import Vec2 as V2         # import with alias
from math import *                         # import all public items
```

### 12.3 Visibility

```
pub     # visible to all modules
(none)  # visible only within current module

pub struct Config:
    pub name: str       # public field
    secret: str         # private field (module only)
```

### 12.4 Project Structure

```
my_project/
    main.atom           # entry point (fn main)
    config.atom          # mod config
    utils/
        strings.atom     # mod utils.strings
        math.atom        # mod utils.math
    lib/
        descriptors/     # C/C++ FFI descriptors (.json)
```

---

## 13. C/C++ Interop (FFI)

### 13.1 Library Descriptors (JSON)

Atom uses JSON descriptor files to interface with C/C++ libraries, evolved from JPLang's system.

```json
// lib/descriptors/sdl2.json
{
    "name": "sdl2",
    "language": "c",
    "headers": ["SDL2/SDL.h"],
    "link": ["-lSDL2"],
    "functions": [
        {
            "name": "SDL_Init",
            "params": [{"name": "flags", "type": "u32"}],
            "return": "i32"
        },
        {
            "name": "SDL_CreateWindow",
            "params": [
                {"name": "title", "type": "ptr[u8]"},
                {"name": "x", "type": "i32"},
                {"name": "y", "type": "i32"},
                {"name": "w", "type": "i32"},
                {"name": "h", "type": "i32"},
                {"name": "flags", "type": "u32"}
            ],
            "return": "ptr[void]"
        }
    ],
    "constants": {
        "SDL_INIT_VIDEO": {"type": "u32", "value": 32},
        "SDL_WINDOWPOS_CENTERED": {"type": "i32", "value": 805240832}
    }
}
```

### 13.2 Using External Libraries

```
import extern "sdl2"

fn main():
    SDL_Init(SDL_INIT_VIDEO)
    let window = SDL_CreateWindow(
        "My App", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        800, 600, 0
    )
```

### 13.3 Unsafe Blocks (for raw FFI)

```
unsafe:
    let raw = malloc(1024) as ptr[u8]
    memcpy(raw, data.as_ptr(), data.len())
    free(raw as ptr[void])
```

### 13.4 Type Mapping (Atom ↔ C)

| Atom Type        | C Type              |
|------------------|---------------------|
| `i8` / `u8`     | `int8_t` / `uint8_t` |
| `i16` / `u16`   | `int16_t` / `uint16_t` |
| `i32` / `u32`   | `int32_t` / `uint32_t` |
| `i64` / `u64`   | `int64_t` / `uint64_t` |
| `f32` / `f64`   | `float` / `double`  |
| `bool`           | `_Bool`             |
| `ptr[T]`         | `T*`                |
| `ptr[void]`      | `void*`             |
| `ptr[fn(...)]`   | Function pointer    |

---

## 14. Concurrency

### 14.1 Async/Await

```
async fn fetch_data(url: str) -> Result[str, Error]:
    let response = await http.get(url)
    return Ok(response.body)

async fn main():
    let data = await fetch_data("https://api.example.com/data")
```

### 14.2 Spawn (lightweight tasks)

```
import std.task

async fn main():
    let handle = task.spawn(async:
        let result = await compute_heavy()
        return result
    )

    let other = do_other_work()
    let result = await handle
```

### 14.3 Channels

```
import std.channel

fn main():
    let (tx, rx) = channel.create[str]()

    task.spawn(async:
        tx.send("hello")
        tx.send("world")
    )

    for msg in rx:
        print("got: {msg}")
```

### 14.4 Thread Safety via Ownership

The ownership system ensures data race freedom at compile time:
- Owned data can only be accessed by one task at a time
- Shared data requires explicit `ref` (immutable) or synchronization primitives
- `mut ref` cannot be shared across tasks

---

## 15. Standard Library Overview

### 15.1 Core Modules

```
std.io          # File I/O, stdin/stdout, buffered readers
std.fs          # Filesystem operations (read, write, mkdir, walk)
std.net         # TCP, UDP sockets
std.http        # HTTP client and server
std.json        # JSON parsing and serialization
std.math        # Math functions, constants
std.time        # Date, time, duration, timers
std.fmt         # String formatting utilities
std.os          # OS interaction, env vars, process
std.path        # Path manipulation (cross-platform)
std.collections # HashMap, HashSet, BTreeMap, VecDeque, etc.
std.thread      # OS-level threading
std.task        # Async task runtime
std.channel     # Message passing channels
std.sync        # Mutex, RwLock, Atomic types
std.crypto      # Hashing, encryption, random
std.regex       # Regular expressions
std.log         # Logging framework
std.test        # Testing framework
std.args        # Command-line argument parsing
```

---

## 16. Memory Layout and Safety Guarantees

### 16.1 Stack vs Heap

- **Stack:** primitives, fixed-size arrays, small structs (compiler decides)
- **Heap:** `str`, `Vec`, `Map`, `Set`, any dynamically-sized data
- **Automatic:** the compiler determines allocation strategy — programmer doesn't choose

### 16.2 Safety Guarantees (compile-time)

- No null pointer dereference (use `Option[T]` instead)
- No use-after-free (ownership + borrow checker)
- No double-free (single owner rule)
- No data races (ownership + borrow rules across tasks)
- No buffer overflows (bounds checking, removable with `unsafe`)
- No uninitialized memory access

### 16.3 Unsafe Escape Hatch

```
unsafe:
    # Raw pointer manipulation
    # Calling C functions directly
    # Bypassing borrow checker
    # Bypassing bounds checks
```

`unsafe` does not disable all checks — only specific operations are unlocked. The surrounding safe code still enforces ownership rules.

---

## 17. Compilation Model

### 17.1 Compiler Pipeline

```
Source (.atom)
    → Lexer (tokens)
    → Parser (AST)
    → Type Checker + Borrow Checker
    → IR (intermediate representation)
    → x86-64 Code Generator (from JPLang backend)
    → Object File (COFF on Windows, ELF on Linux)
    → Linker → Executable
```

### 17.2 Build

```bash
atom build main.atom                  # compile to executable
atom build main.atom -o myapp         # custom output name
atom build main.atom --release        # optimized build
atom build main.atom --target=linux   # cross-compile target
atom run main.atom                    # compile and run
atom check main.atom                  # type check only (no codegen)
```

### 17.3 FFI Linking

```bash
atom build main.atom --lib sdl2 --lib opengl
# Reads descriptors from lib/descriptors/
# Links with specified libraries
```

---

## 18. Example Programs

### 18.1 Hello World

```
fn main():
    print("Hello, World!")
```

### 18.2 Fibonacci

```
fn fib(n: i32) -> i32:
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)

fn main():
    for i in 0..20:
        print("fib({i}) = {fib(i)}")
```

### 18.3 File Processing

```
from std.fs import read_file, write_file
from std.json import parse, stringify

struct Config:
    name: str
    version: str
    debug: bool

fn main():
    let text = read_file("config.json") or:
        panic("could not read config")

    let config: Config = parse(text) or:
        panic("invalid config format")

    print("Running {config.name} v{config.version}")

    if config.debug:
        print("Debug mode enabled")
```

### 18.4 HTTP Server

```
from std.http import Server, Request, Response

fn handle(req: Request) -> Response:
    match req.path:
        "/":
            return Response.ok("Welcome to Atom!")
        "/api/data":
            let data = map!{"status": "ok", "language": "atom"}
            return Response.json(data)
        _:
            return Response.not_found("404 - Not Found")

fn main():
    let server = Server.bind("0.0.0.0:8080")
    print("Listening on :8080")
    server.serve(handle)
```

### 18.5 Ownership in Action

```
struct Buffer:
    data: Vec[u8]

impl Buffer:
    fn new(size: i32) -> Buffer:
        return Buffer(data=Vec.with_capacity(size))

    fn write(mut self, bytes: ref [u8]):
        self.data.extend(bytes)

    fn consume(self) -> Vec[u8]:     # takes ownership, self is moved
        return self.data

fn main():
    let mut buf = Buffer.new(1024)
    buf.write(ref [72, 101, 108, 108, 111])

    let data = buf.consume()         # buf is moved, no longer usable
    # buf.write(ref [1, 2, 3])       # ERROR: buf has been moved
    print("bytes: {data}")
```

---

## 19. Future Considerations (Post v1.0)

- **Package manager** (`atom pkg install ...`)
- **REPL** (interactive mode for quick experimentation)
- **LSP server** (IDE support: autocomplete, go-to-definition, inline errors)
- **Formatter** (`atom fmt`)
- **Documentation generator** (`atom doc`)
- **WebAssembly target** (compile to WASM)
- **GPU compute** (shader/compute kernel support)
- **Hot reload** (for development iteration)

---

*Atom Language Specification v0.1 — Draft*
*Targeting: x86-64 native (Windows PE + Linux ELF)*
*Backend: JPLang code generator*
