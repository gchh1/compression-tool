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