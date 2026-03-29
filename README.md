# compression-tool

## project layout
```
. /
├── build/
├── docs/                    # 任务书、设计报告等
├── tests/                    # 测试  
├── third_party/         # 放 pybind11 等第三方库源码
├── src/
│   ├── bindings/       # pybind / wasm 胶水代码
│   ├── core/              # 核心 C++ 算法
│   │   ├── include/      
│   │   └── src/          
│   └── gui/                # 前端代码 
├── CMakeLists.txt
├── .gitignore
└── README.md
```

new
```
.\
├── bin/                  <-- 二进制文件和可执行程序文件
├── files/                <-- 用于压缩文件，打包好的程序文件夹子下也应有这个，或者指定路径
├── include/              <-- 全局的、对外的公共头文件 (给别人引用的 API)
│   └── lz77_core.h       <-- 暴露出 lz77Compress 和结构体  
├── output/               <-- 存放压缩输出/或者指定输出目录（非本目录）
├── src/
│   ├── bindings/         <-- 所有的跨语言胶水代码
│   │    └── python/
│   │       └── lz77_pybind.cpp  <-- 只在这里 #include <pybind11/...>
│   ├── cores/            <-- 纯净的核心 C++ 实现，没有任何 Python 痕迹
│   │   ├── lz77_core.cpp <-- 核心实现
│   │   └── internal.h    <-- (可选) 如果有内部类比如 BruteForce 的头文件
│   ├── gui/ # 保留的设计，可能没有用
│   ├── test_lz77.py
│   └── test_site_compression.py
├── Makefile              <-- 只需要调整一下路径即可
├── setup.py              <-- 指向 bindings/python/ 和 src/cores/
└── README.md
```
