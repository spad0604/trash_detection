# Test best_model.pt on Windows

Web app nhỏ để upload ảnh và detect bằng model đang dùng trên Pi:

```text
robot/Pi/trash_sorting_ros/models/best_model.pt
```

## Cài thư viện

```powershell
cd "D:\Trash Detection\model_best_pt_test"
python -m pip install -r requirements.txt
```

## Chạy app

```powershell
streamlit run app.py
```

Hoặc double click:

```text
run_windows.bat
```

Sau đó mở URL Streamlit hiện ra, thường là:

```text
http://localhost:8501
```

## Mapping 3 ngăn

Model hiện có class:

```text
BIODEGRADABLE, CARDBOARD, GLASS, METAL, PAPER, PLASTIC
```

Mapping dùng để hiển thị:

```text
BIODEGRADABLE -> Hữu cơ
CARDBOARD, METAL, PAPER, PLASTIC -> Tái chế
GLASS, unknown -> Khác
```
