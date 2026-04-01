# Python Interface + Visualization Delivery

This project now includes a runnable Python UI prototype with a shared backend service layer.

## New Apps

- `src/ui/pyqt_app.py`: Desktop app (drag-drop, resource list, compress/decompress, animated demo visualization)

## Shared Service Layer

- `src/ui/services/compression_service.py`
- `src/ui/services/models.py`

The app reuses:
- resource staging to `resources/staging/`
- compression output to `compressed/`
- decompression output to `decompressed/`
- LZ77 demo steps generated from `resources/test1.txt`

## Run

From project root, use:

- `run_ui_pyqt.bat`

## Notes

- PyQt app supports drag-and-drop for both files and folders.
- Compression artifacts are saved as `.lz77` (single compact binary container).
