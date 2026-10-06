using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;

namespace UbpdRemote {
    // Преобразование GIF/картинок в кадры для OLED UBPD (1-бит, 128x64).
    // Вся работа — на ПК. ESP получает уже готовые 1024-байтные кадры и просто
    // копирует их в буфер дисплея (никаких вычислений на устройстве).
    //
    // Формат кадра = раскладка буфера U8g2 для SSD1306 (родная GDDRAM):
    //   byte[(y>>3) * 128 + x], бит (y & 7) = пиксель (x,y).
    public static class Oled {
        public const int W = 128;
        public const int H = 64;
        public const int FrameBytes = (W * H) / 8; // 1024

        public static (List<byte[]> frames, List<int> delaysMs) LoadGif(string path, int threshold = 128) {
            var frames = new List<byte[]>();
            var delays = new List<int>();
            using var img = Image.FromFile(path);
            var dim = new FrameDimension(img.FrameDimensionsList[0]);
            int count = img.GetFrameCount(dim);
            for (int f = 0; f < count; f++) {
                img.SelectActiveFrame(dim, f);
                using var bmp = new Bitmap(W, H, PixelFormat.Format24bppRgb);
                using (var g = Graphics.FromImage(bmp)) {
                    g.Clear(Color.Black);
                    float s = Math.Min((float)W / img.Width, (float)H / img.Height);
                    int dw = Math.Max(1, (int)(img.Width * s));
                    int dh = Math.Max(1, (int)(img.Height * s));
                    g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                    g.DrawImage(img, (W - dw) / 2, (H - dh) / 2, dw, dh);
                }
                frames.Add(Pack(bmp, threshold));
                delays.Add(FrameDelay(img, f));
            }
            return (frames, delays);
        }

        public static byte[] LoadImage(string path, int threshold = 128) {
            using var img = Image.FromFile(path);
            using var bmp = new Bitmap(W, H, PixelFormat.Format24bppRgb);
            using (var g = Graphics.FromImage(bmp)) {
                g.Clear(Color.Black);
                float s = Math.Min((float)W / img.Width, (float)H / img.Height);
                int dw = Math.Max(1, (int)(img.Width * s));
                int dh = Math.Max(1, (int)(img.Height * s));
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.DrawImage(img, (W - dw) / 2, (H - dh) / 2, dw, dh);
            }
            return Pack(bmp, threshold);
        }

        private static byte[] Pack(Bitmap bmp, int threshold) {
            var buf = new byte[FrameBytes];
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    var c = bmp.GetPixel(x, y);
                    int lum = (c.R * 30 + c.G * 59 + c.B * 11) / 100;
                    if (lum >= threshold) buf[(y >> 3) * W + x] |= (byte)(1 << (y & 7));
                }
            }
            return buf;
        }

        // Задержка кадра GIF: свойство 0x5100, в 1/100 секунды (массив на кадр).
        private static int FrameDelay(Image img, int frame) {
            try {
                var pi = img.GetPropertyItem(0x5100);
                if (pi.Value.Length >= (frame + 1) * 4) {
                    int cs = BitConverter.ToInt32(pi.Value, frame * 4);
                    int ms = cs * 10;
                    if (ms > 0) return ms;
                }
            } catch { }
            return 100;
        }
    }
}
