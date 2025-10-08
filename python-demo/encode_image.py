import sys
import base64
from pathlib import Path

def encode_image_to_base64(image_path: str, output_path: str = "output.txt"):
    image_file = Path(image_path)
    if not image_file.exists():
        print(f"❌ 指定されたファイルが存在しません: {image_file}")
        return

    # 画像を読み込み & Base64エンコード
    with open(image_file, "rb") as f:
        encoded = base64.b64encode(f.read()).decode("utf-8")

    # テキストファイルに保存
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(encoded)

    print(f"✅ エンコード完了！")
    print(f"入力画像: {image_file.resolve()}")
    print(f"出力テキスト: {Path(output_path).resolve()}")
    print(f"文字数: {len(encoded):,} chars")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("使い方: python encode_image.py <画像ファイルパス> [出力ファイル名]")
        sys.exit(1)

    image_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else "output.txt"

    encode_image_to_base64(image_path, output_path)
