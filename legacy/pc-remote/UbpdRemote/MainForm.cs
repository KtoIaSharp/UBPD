using System;
using System.Collections.Generic;
using System.Drawing;
using System.Threading;
using System.Windows.Forms;
using InTheHand.Net;

namespace UbpdRemote {
    public class MainForm : Form {
        private readonly BtLink _bt = new BtLink();

        private readonly ComboBox _devs = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 240 };
        private readonly Button _refresh = new Button { Text = "Обновить", Width = 90 };
        private readonly Button _connect = new Button { Text = "Подключить", Width = 110 };
        private readonly Label _status = new Label { Text = "не подключено", AutoSize = true, ForeColor = Color.Firebrick };

        private readonly TextBox _log = new TextBox { Multiline = true, ScrollBars = ScrollBars.Vertical, ReadOnly = true, Dock = DockStyle.Fill, BackColor = Color.FromArgb(20, 20, 20), ForeColor = Color.Gainsboro, Font = new Font("Consolas", 9) };

        private readonly TextBox _text = new TextBox { Text = "ПРИВЕТ UBPD", Width = 300 };
        private readonly NumericUpDown _secs = new NumericUpDown { Minimum = 1, Maximum = 60, Value = 5, Width = 60 };

        private readonly TextBox _gifPath = new TextBox { ReadOnly = true, Width = 300 };
        private readonly NumericUpDown _gifDelay = new NumericUpDown { Minimum = 0, Maximum = 5000, Value = 0, Width = 70 };
        private readonly Label _gifInfo = new Label { Text = "GIF не выбран", AutoSize = true };
        private readonly Button _send = new Button { Text = "Отправить анимацию", Width = 170 };

        private readonly TextBox _state = new TextBox { Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill, BackColor = Color.FromArgb(20, 20, 20), ForeColor = Color.Gainsboro, Font = new Font("Consolas", 9) };

        private List<byte[]> _frames;
        private List<int> _delays;
        private volatile bool _busy;

        public MainForm() {
            Text = "UBPD PC REMOTE (Bluetooth SPP)";
            Width = 600;
            Height = 640;
            StartPosition = FormStartPosition.CenterScreen;

            // ---- верх: подключение ----
            var top = new Panel { Dock = DockStyle.Top, Height = 44 };
            _refresh.Left = 8; _refresh.Top = 10; _refresh.Click += (s, e) => RefreshDevices();
            _devs.Left = 104; _devs.Top = 11;
            _connect.Left = 350; _connect.Top = 10; _connect.Click += (s, e) => ToggleConnect();
            _status.Left = 468; _status.Top = 14;
            top.Controls.AddRange(new Control[] { _refresh, _devs, _connect, _status });

            // ---- вкладки ----
            var tabs = new TabControl { Dock = DockStyle.Fill };

            // Текст
            var t1 = new TabPage("Текст на экран");
            var l1 = new Label { Text = "Текст:", Left = 12, Top = 20, AutoSize = true };
            var l1b = new Label { Text = "секунд:", Left = 12, Top = 56, AutoSize = true };
            var bShow = new Button { Text = "Показать", Left = 12, Top = 92, Width = 120 };
            var bClr = new Button { Text = "Стереть", Left = 144, Top = 92, Width = 120 };
            _text.Left = 70; _text.Top = 18;
            _secs.Left = 70; _secs.Top = 54;
            bShow.Click += (s, e) => SendText();
            bClr.Click += (s, e) => SafeSend("{\"cmd\":\"clear\"}");
            t1.Controls.AddRange(new Control[] { l1, _text, l1b, _secs, bShow, bClr });

            // GIF
            var t2 = new TabPage("GIF / анимация");
            var bOpen = new Button { Text = "Открыть GIF...", Left = 12, Top = 20, Width = 140 };
            _gifPath.Left = 160; _gifPath.Top = 22;
            _gifInfo.Left = 12; _gifInfo.Top = 58;
            var l2 = new Label { Text = "мс/кадр (0 = из GIF):", Left = 12, Top = 92, AutoSize = true };
            _gifDelay.Left = 190; _gifDelay.Top = 90;
            _send.Left = 12; _send.Top = 124; _send.Enabled = false;
            bOpen.Click += (s, e) => OpenGif();
            _send.Click += (s, e) => SendAnim();
            t2.Controls.AddRange(new Control[] { bOpen, _gifPath, _gifInfo, l2, _gifDelay, _send });

            // Статус
            var t3 = new TabPage("Статус");
            var bState = new Button { Text = "Запросить статус", Left = 12, Top = 12, Width = 160, Dock = DockStyle.Top };
            bState.Click += (s, e) => SafeSend("{\"cmd\":\"state\"}");
            t3.Controls.Add(_state);
            t3.Controls.Add(bState);

            tabs.TabPages.Add(t1);
            tabs.TabPages.Add(t2);
            tabs.TabPages.Add(t3);

            // ---- лог ----
            var logBox = new GroupBox { Text = "Журнал", Dock = DockStyle.Bottom, Height = 150 };
            logBox.Controls.Add(_log);

            Controls.Add(tabs);
            Controls.Add(logBox);
            Controls.Add(top);

            _bt.Log += m => Ui(() => Append(m));
            _bt.Line += m => Ui(() => { Append("<= " + m); _state.AppendText(m + Environment.NewLine); });

            Append("Готово. Выбери устройство и нажми «Подключить».");
        }

        private void Ui(Action a) {
            if (InvokeRequired) BeginInvoke(a); else a();
        }

        private void Append(string s) {
            _log.AppendText(DateTime.Now.ToString("HH:mm:ss") + "  " + s + Environment.NewLine);
        }

        private void RefreshDevices() {
            try {
                _devs.Items.Clear();
                foreach (var (name, addr) in _bt.Discover()) {
                    _devs.Items.Add(new DevItem(name, addr));
                }
                Append($"Найдено устройств: {_devs.Items.Count}");
                if (_devs.Items.Count > 0) _devs.SelectedIndex = 0;
            } catch (Exception ex) {
                Append("Ошибка поиска: " + ex.Message);
            }
        }

        private void ToggleConnect() {
            if (_bt.Connected) {
                _bt.Close();
                _connect.Text = "Подключить";
                _status.Text = "не подключено";
                _status.ForeColor = Color.Firebrick;
                return;
            }
            if (_devs.SelectedItem is not DevItem d) { Append("Сначала выбери устройство."); return; }
            if (_bt.Connect(d.Addr)) {
                _connect.Text = "Отключить";
                _status.Text = "подключено: " + d.Name;
                _status.ForeColor = Color.SeaGreen;
            }
        }

        private void SafeSend(string json) {
            try {
                if (!_bt.Connected) { Append("Нет соединения."); return; }
                _bt.SendLine(json);
                Append("=> " + json);
            } catch (Exception ex) {
                Append("Ошибка: " + ex.Message);
            }
        }

        private static string Esc(string s) =>
            s.Replace("\\", "\\\\").Replace("\"", "\\\"");

        private void SendText() {
            string json = "{\"cmd\":\"oled\",\"text\":\"" + Esc(_text.Text) + "\",\"secs\":" + (int)_secs.Value + "}";
            SafeSend(json);
        }

        private void OpenGif() {
            using var dlg = new OpenFileDialog { Filter = "GIF|*.gif|Все файлы|*.*" };
            if (dlg.ShowDialog() != DialogResult.OK) return;
            try {
                var (frames, delays) = Oled.LoadGif(dlg.FileName);
                _frames = frames;
                _delays = delays;
                _gifPath.Text = dlg.FileName;
                _gifInfo.Text = $"кадров: {frames.Count}, ~{frames.Count * Oled.FrameBytes / 1024} КБ";
                _send.Enabled = true;
                Append($"GIF загружен: {frames.Count} кадров");
            } catch (Exception ex) {
                Append("Ошибка GIF: " + ex.Message);
            }
        }

        private void SendAnim() {
            if (_frames == null || _busy) return;
            if (!_bt.Connected) { Append("Нет соединения."); return; }
            _busy = true;
            _send.Enabled = false;
            var frames = _frames;
            var delays = _delays;
            int fixedDelay = (int)_gifDelay.Value;
            var t = new Thread(() => {
                try {
                    _bt.SendLine("{\"cmd\":\"anim\",\"n\":" + frames.Count + "}");
                    Append($"=> anim n={frames.Count}");
                    Thread.Sleep(50); // дать ESP переключиться в режим кадров
                    for (int i = 0; i < frames.Count; i++) {
                        _bt.Send(frames[i]);
                        int d = fixedDelay > 0 ? fixedDelay : delays[i];
                        Thread.Sleep(d);
                    }
                    Append("Анимация отправлена.");
                } catch (Exception ex) {
                    Append("Ошибка анимации: " + ex.Message);
                } finally {
                    Ui(() => { _busy = false; _send.Enabled = true; });
                }
            }) { IsBackground = true };
            t.Start();
        }

        private sealed class DevItem {
            public readonly string Name;
            public readonly BluetoothAddress Addr;
            public DevItem(string n, BluetoothAddress a) { Name = n; Addr = a; }
            public override string ToString() => Name + "  [" + Addr + "]";
        }
    }
}
