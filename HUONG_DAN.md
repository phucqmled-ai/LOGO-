# QM Logo Runner – Plugin chạy logo & chữ cho Resolume Arena 7

Plugin FFGL dạng **Source**: chạy logo PNG và chữ (có tiếng Việt) theo đường **ngang, dọc, tròn, elip, viền chữ nhật**, có viền phát sáng như hiệu ứng "border chạy". Dùng được trên **Windows** và **macOS** (cả Apple Silicon lẫn Intel).

---

## 1. Lấy file plugin (không cần biết code)

Plugin phải được "build" thành file `.dll` (Windows) và `.bundle` (Mac). GitHub sẽ làm việc này tự động và miễn phí:

1. Tạo tài khoản tại **github.com** (nếu chưa có).
2. Bấm **+** (góc trên phải) → **New repository** → đặt tên `QMLogoRunner` → **Create repository**.
3. Bấm **uploading an existing file** → kéo **toàn bộ nội dung** thư mục `LogoRunner` vào (gồm cả thư mục ẩn `.github`) → **Commit changes**.
   - Mac: thư mục `.github` bị ẩn, bấm `Cmd + Shift + .` trong Finder để hiện.
   - Nếu upload không nhận thư mục `.github`: vào **Add file → Create new file**, gõ tên `.github/workflows/build.yml`, dán nội dung file `build.yml` vào → Commit.
4. Vào tab **Actions** → chờ khoảng 3–5 phút đến khi có dấu ✅ xanh.
5. Bấm vào lần chạy đó → kéo xuống mục **Artifacts** → tải:
   - `QMLogoRunner-Windows` → giải nén ra `QMLogoRunner.dll`
   - `QMLogoRunner-macOS` → giải nén ra `QMLogoRunner.bundle`

## 2. Cài vào Resolume

Copy file vào thư mục **Extra Effects** rồi mở lại Resolume:

| Hệ điều hành | Thư mục |
|---|---|
| Windows | `Documents\Resolume Arena\Extra Effects\` |
| macOS | `Documents/Resolume Arena/Extra Effects/` |

Nếu chưa có thư mục thì tự tạo. Hoặc chỉ đường dẫn khác trong **Preferences → Video**.

**Riêng Mac:** nếu Resolume không thấy plugin, mở Terminal và chạy:
```
xattr -dr com.apple.quarantine ~/Documents/"Resolume Arena"/"Extra Effects"/QMLogoRunner.bundle
```

Trong Resolume: tab **Sources** → tìm **QM Logo Runner** → kéo vào một clip.

## 3. Các thông số

**Content (nội dung)**
| Thông số | Ý nghĩa |
|---|---|
| Logo 1 / 2 / 3 | Chọn file PNG (nên nền trong suốt) hoặc JPG. Plugin tự cắt bỏ phần trong suốt thừa. |
| Text | Chữ chạy. Dùng dấu **\|** để tách nhiều câu, ví dụ `QM LED \| Chào mừng quý khách \| HOTLINE 0909…` |
| Font | Chọn file font `.ttf/.otf`. Để trống thì dùng Arial Bold của máy. |
| Content | Logo + Text xen kẽ / Lần lượt (hết logo rồi tới chữ) / Chỉ logo / Chỉ text |
| Text Color | Màu chữ |

**Shape (hình dạng)**
| Thông số | Ý nghĩa |
|---|---|
| Shape | Ngang, Dọc, Tròn, Elip, Viền chữ nhật |
| Position X / Y | Tâm của đường chạy (hiện số pixel) |
| Width / Height | Kích thước khung (Tròn chỉ dùng Height làm đường kính) |
| Thickness | Độ dày dải chạy |
| Item Size | Logo/chữ to nhỏ trong dải |
| Gap | Khoảng cách giữa các logo/chữ |
| Flip Side | Lật mặt chữ (đầu chữ quay vào trong) |

**Motion (chuyển động)**
| Thông số | Ý nghĩa |
|---|---|
| Speed | 0% là đứng yên, dương chạy một chiều, âm chạy ngược lại |
| Offset | Dịch vị trí bắt đầu, dùng để các màn chạy lệch nhau |

**Border (viền)**: bật/tắt viền, màu viền, độ dày, độ phát sáng (Glow), và **Band Fill** để tô nền đen cho dải chạy.

Mọi thông số đều gán được MIDI/OSC, BPM sync và animation như effect thường của Resolume.

## 4. Chạy theo từng slice màn hình

Plugin vẽ lên composition, còn **Advanced Output** quyết định slice nào lấy phần nào.

**Chạy chung (tất cả slice cùng một nội dung)**
1. Đặt 1 clip QM Logo Runner trên 1 layer.
2. Chọn Shape và chỉnh Position/Width/Height cho dải chạy trùng với vùng mà các slice đang lấy (Input Selection trong Advanced Output).

**Chạy riêng (mỗi slice nội dung khác nhau)**
1. Mỗi màn hình dùng 1 layer riêng, mỗi layer 1 clip QM Logo Runner với logo/chữ/màu/tốc độ riêng.
2. Trong **Advanced Output**, chọn slice → ở mục **Input Source** chọn đúng layer đó.
3. Hoặc giữ chung composition nhưng đặt mỗi clip ở một vùng khác nhau (Position/Width/Height), mỗi slice lấy đúng vùng của mình.

**Mẹo cho màn hình tùy biến (hình lạ)**
- Màn dạng dải dài: dùng Ngang hoặc Dọc.
- Màn bao quanh sân khấu, khung, cột vuông: dùng **Viền chữ nhật**.
- Màn tròn, vòng LED: dùng **Tròn/Elip**.
- Hình phức tạp hơn: ghép nhiều clip (mỗi đoạn một Shape), hoặc chạy dải Ngang rồi dùng **Mask/Warp** của slice trong Advanced Output để uốn theo hình màn.
- Cùng một nội dung cho nhiều màn nhưng lệch nhịp: nhân bản clip và chỉnh **Offset** khác nhau.

## 5. Lỗi thường gặp

- **Không hiện chữ:** máy thiếu Arial → chọn một file font ở mục Font.
- **Logo không hiện:** kiểm tra file PNG/JPG có mở được không, thử đổi tên file/thư mục không dấu.
- **Logo bị vỡ khi rất to:** plugin lưu logo ở chiều cao 256 px. Muốn sắc nét hơn thì sửa `ROW_H` trong `src/LogoRunner.cpp` (ví dụ 512) rồi build lại.

---
Mã nguồn: `src/LogoRunner.cpp`. Dựa trên Resolume FFGL SDK (giấy phép trong `ffgl/LICENSE.md`), stb_image và stb_truetype (public domain).
