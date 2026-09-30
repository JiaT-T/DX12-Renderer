# Third-party content inventory

This records attribution and evidence without relicensing content.

| Content | Repository evidence / remaining work |
| --- | --- |
| `third_party/tiny_obj_loader.h`, `third_party/stb_image.h` | Embedded original license notices retained. |
| Dear ImGui 1.90.9 | Headers identify this version. Missing MIT `LICENSE.txt` restored from [exact upstream tag](https://raw.githubusercontent.com/ocornut/imgui/v1.90.9/LICENSE.txt). |
| ImGuizmo | MIT notice embedded in header/source. |
| `DXMath` types | Microsoft MIT notices, MiniEngine attribution in headers; root project license does not replace these notices. |
| `d3dx12.h`, `DDSTextureLoader.*` | Microsoft copyright/source notices retained. Exact original upstream revision/license package is not recorded. |
| `DXMath/MathHelper.*` | Frank Luna attribution and All Rights Reserved notice present. Confirm sample-code redistribution terms; cleanup does not alter that notice. |
| `Textures/*.dds`, `Textures/tree*.bmp`, `Models/skull.txt` | Sample resource provenance/license not documented. Confirm original acquisition and redistribution terms. |
| `Models/Obj_PBRTest/textures` | ChristmasTreeOrnament019-named asset files; provider/download/license records absent. |
| Sponza / HDR / Metal1 external resources | Not copied or newly redistributed by this cleanup. `DX12_ASSET_ROOT` loads user-supplied resources; owner must retain their individual licenses. |
| `README_Assets/sponza_preview.png` | Existing author-rendered preview retained; recreating it requires external scene resources. |
| Procedural default environment | Generated from small constant float colors in code, no third-party HDR asset. |

Unverified provenance is a follow-up item, not a new grant. The repository LICENSE and all original copyright statements remain unchanged.
