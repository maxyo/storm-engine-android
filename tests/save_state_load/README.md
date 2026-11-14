# Loading a script state the program cannot take as a whole

`COMPILER::LoadState` used to read the variables straight into the freshly loaded program,
after the running game had been erased. One variable it could not take shifted the reading of
every later one, and references were bound before the arrays they point into had their saved
size (audit 2026-11-14, #6 and #7):

- a global whose type changed at run time (`int i; i += 0.5;` makes it a float, `DATA::Plus`)
  was refused at its type check without consuming its value: the next name was read from the
  middle of it, the load failed with the world already gone;
- a saved variable the program no longer declares was skipped only for int, float, string and
  object values (arrays of refs, arefs, pointers desynchronised the stream);
- a `ref` / `aref` declared before the array it points into, with the array saved larger than
  declared (`SetArraySize`, another script version), was bound to the old storage: the resize
  then reallocated it (heap-use-after-free), or the element did not exist yet (null access).

Now `save_state.hpp` parses the whole state before anything is touched (`CoreImpl` refuses a
state that does not parse while the game is still there), then applies it in three passes: array
sizes, values, references. The check saves globals with the production writer methods of
`compiler.cpp` (extracted by `run.py`) and loads them with `save_state.hpp`, under ASan/UBSan:

- drifted scalars come back as saved, the variables after them intact;
- refs and arefs into arrays saved larger than declared name the resized storage;
- saved variables the program does not declare (every type, arrays of refs/arefs/pointers) or
  declares differently are reported by name and left, all other variables load;
- references to a variable or element the program does not have are reported and left empty;
- an array element whose type changed at run time is written converted to the array's type,
  with an error line (elements carry no type of their own in the save);
- an aref to an object's root attribute names the root again;
- truncated states, trailing bytes and unknown value types are refused by `parseState`.

```sh
python3 tests/save_state_load/run.py --dependency-sources <sources> --glm-source <glm>
```
