# KNIGHT

UCI chess engine with one search worker and a compressed launcher under 8,192 bytes.

Requires Linux, a C++20-capable `g++`, Python 3, and `xz`.

```sh
make
./k8e
```

Use `./k8e_release` for the compact launcher, which compiles and caches the native engine on first run.

Tests:

```sh
python3 -m pip install -r tests/requirements.txt
make test
```

Based on Ciekals' [c4ke](https://github.com/ciekals/c4ke), with search inspired by Andrew Grant's [Ethereal](https://github.com/AndyGrant/Ethereal). Licensed under [GPL-3.0](LICENSE).
