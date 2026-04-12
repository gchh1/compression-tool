# compression-tool

## project layout
```
. /
├── build/
├── docs/                     # 任务书、设计报告等
├── tests/                    # 测试  
├── third_party/              # 放 pybind11 等第三方库源码
├── src/
│   ├── bindings/             # pybind / wasm 胶水代码
│   ├── algorithm/            # lz77, humman 等基础算法
│   ├── core/                 # 压缩算法
│   │   ├── include/      
│   │   └── src/          
│   └── gui/                  # 前端代码 
├── CMakeLists.txt
├── .gitignore
└── README.md
```

## started
```
mkdir build_wasm && cd build_wasm
```

```
emcmake cmake ..
make
```

## log
### 4.12
目前完成了基本的Deflate压缩算法，在core中提供了```CompressorResult Compress(const std::vector<uint8_t>&)```的压缩 API 以及 ```CompressorResult Decompress(const std::vector<uint8_t>&)``` 的解压缩 API 

并且在```build_wasm/src/bindings```中构建了 html 前端