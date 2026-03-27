from setuptools import Extension, setup
import pybind11


ext_modules = [
    Extension(
        "c_api_test",
        ["c_api_test_pybind.cpp"],
        include_dirs=[pybind11.get_include()],
        language="c++",
    )
]


setup(
    name="c_api_test",
    version="0.1.0", # 用于标注当前dll的版本
    ext_modules=ext_modules,
)

