"""Perceptually-uniform scientific colormaps for data visualization.



Provides the Viridis colormap as a 256-entry lookup table ΓÇö perceptually

uniform, colorblind-friendly, and print-safe.  Suitable for continuous

scalar fields such as Shannon entropy.



Usage::



    from gui.ui.views.visualizers.colormap import viridis_color

    color: QColor = viridis_color(0.5)  # t in [0, 1]

"""



from __future__ import annotations



from PyQt6.QtGui import QColor



# ΓöÇΓöÇ Viridis colormap ΓÇö 256 RGB triplets (0ΓÇô255) ΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇ

# Source: matplotlib / BIDS colormap (CC0)

# Perceptually uniform sequential colormap: purple ΓåÆ teal ΓåÆ green ΓåÆ yellow



_VIRIDIS_DATA = bytes.fromhex(

    "44015444025544035745055845065a45085b46095c460b5e460c5f460e61470f"

    "6247116347126547146647156747166947186a48196b481a6c481c6e481d6f48"

    "1e70482071482172482273482374472575472676472777472878472a79472b7a"

    "472c7b462d7c462f7c46307d46317e45327f45347f4535804536814437814439"

    "82433a83433b83433c84423d84423e854240854141864142864043874044873f"

    "45873f47883e48883e49893d4a893d4b893d4c893c4d8a3c4e8a3b508a3b518a"

    "3a528b3a538b39548b39558b38568b38578c37588c37598c365a8c365b8c355c"

    "8c355d8c345e8d345f8d33608d33618d32628d32638d31648d31658d31668d30"

    "678d30688d2f698d2f6a8d2e6b8e2e6c8e2e6d8e2d6e8e2d6f8e2c708e2c718e"

    "2c728e2b738e2b748e2a758e2a768e2a778e29788e29798e287a8e287a8e287b"

    "8e277c8e277d8e277e8e267f8e26808e26818e25828e25838d24848d24858d24"

    "868d23878d23888d23898d22898d228a8d228b8d218c8d218d8c218e8c208f8c"

    "20908c20918c1f928c1f938b1f948b1f958b1f968b1e978a1e988a1e998a1e99"

    "8a1e9a891e9b891e9c891e9d881e9e881e9f881ea0871fa1871fa2861fa38620"

    "a48520a58521a68521a78422a78423a88323a98224aa8225ab8126ac8127ad80"

    "28ae7f29af7f2ab07e2bb17d2cb17d2eb27c2fb37b30b47a32b57a33b67935b7"

    "7836b87738b97639b9763bba753dbb743ebc7340bd7242be7144be7045bf6f47"

    "c06e49c16d4bc26c4dc26b4fc36951c46853c56755c66657c66559c7645bc862"

    "5ec96160c96062ca5f64cb5d67cc5c69cc5b6bcd596dce5870ce5672cf5574d0"

    "5477d05279d1517cd24f7ed24e81d34c83d34b86d44988d5478bd5468dd64490"

    "d64392d74195d73f97d83e9ad83c9dd93a9fd938a2da37a5da35a7db33aadb32"

    "addc30afdc2eb2dd2cb5dd2bb7dd29bade27bdde26bfdf24c2df22c5df21c7e0"

    "1fcae01ecde01dcfe11cd2e11bd4e11ad7e219dae218dce218dfe318e1e318e4"

    "e318e7e419e9e419ece41aeee51bf1e51cf3e51ef6e61ff8e621fae622fde724"

)



assert len(_VIRIDIS_DATA) == 768, f"Expected 768 bytes, got {len(_VIRIDIS_DATA)}"



_VIRIDIS_256: list[QColor] = []

for _i in range(256):

    _r = _VIRIDIS_DATA[_i * 3]

    _g = _VIRIDIS_DATA[_i * 3 + 1]

    _b = _VIRIDIS_DATA[_i * 3 + 2]

    _VIRIDIS_256.append(QColor(_r, _g, _b))





def viridis_color(t: float) -> QColor:

    """Map a normalised value *t* Γêê [0, 1] to a Viridis ``QColor``.



    Values outside [0, 1] are clamped.  Lookup is O(1) nearest-neighbour

    into the 256-entry LUT ΓÇö zero allocations per call after warm-up.

    """

    idx = max(0, min(255, int(t * 255.0)))

    return _VIRIDIS_256[idx]

