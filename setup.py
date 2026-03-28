from setuptools import setup, Extension
import pybind11

ext_modules = [
    Extension(
        "lz77",
        ["lty_cores/lz77_core.cpp", "lty_cores/lz77_pybind.cpp"],
        include_dirs=[
            pybind11.get_include(),
            "lty_cores"
        ],
        language="c++",
        extra_compile_args=["/std:c++14"] # For MSVC
    )
]

setup(
    name="lz77",
    version="0.0.1",
    author="Lam Tim",
    description="LZ77 algorithm with pybind11",
    ext_modules=ext_modules,
)
