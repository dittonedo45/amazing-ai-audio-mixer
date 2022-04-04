from distutils.core import setup, Extension

setup(name="aaai", ext_modules=[
    Extension ('aaai',
    [ 'main.c' ],
    libraries = [
        "avutil",
        "avformat",
        "avcodec",
        "avfilter"
        ]
    )
    ]);
