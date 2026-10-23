# KNIGHT

单搜索线程 UCI 国际象棋引擎，压缩启动器小于 8,192 字节。

依赖 Linux、支持 C++20 的 `g++`、Python 3 和 `xz`。

```sh
make
./k8e
```

也可运行 `./k8e_release`，首次启动会编译并缓存原生引擎。

测试：

```sh
python3 -m pip install -r tests/requirements.txt
make test
```

基于 Ciekals 的 [c4ke](https://github.com/ciekals/c4ke)，搜索参考 Andrew Grant 的 [Ethereal](https://github.com/AndyGrant/Ethereal)。采用 [GPL-3.0](LICENSE) 许可。
