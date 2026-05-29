# -*- mode: python ; coding: utf-8 -*-


a = Analysis(
    ['..\\..\\src\\gui\\main.py'],
    pathex=[],
    binaries=[],
    datas=[
        ('Package\\build_pyinst\\core_engine_new.cp312-win_amd64.pyd', '.'),
        ('Package\\build_pyinst\\libgcc_s_seh-1.dll', '.'),
        ('Package\\build_pyinst\\libstdc++-6.dll', '.'),
        ('Package\\build_pyinst\\libwinpthread-1.dll', '.'),
        ('..\\..\\src\\gui\\ui\\views\\html\\deflate_dashboard.html', 'gui\\ui\\views\\html'),
        ('..\\..\\src\\gui\\ui\\views\\html\\d3.v7.min.js', 'gui\\ui\\views\\html'),
    ],
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='WebCompress',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
