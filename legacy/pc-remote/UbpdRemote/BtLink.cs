using System;
using System.Collections.Generic;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using InTheHand.Net;
using InTheHand.Net.Bluetooth;
using InTheHand.Net.Sockets;

namespace UbpdRemote {
    // Связь с UBPD по классическому Bluetooth (SPP / RFCOMM).
    // Протокол: одна JSON-строка = одна команда, '\n' в конце.
    public class BtLink : IDisposable {
        private BluetoothClient _cli;
        private NetworkStream _stream;
        private Thread _rx;

        public bool Connected => _cli != null && _cli.Connected;

        public event Action<string> Log;
        public event Action<string> Line;   // входящая строка (ответы прибора)

        public List<(string name, BluetoothAddress addr)> Discover() {
            var res = new List<(string, BluetoothAddress)>();
            using var cli = new BluetoothClient();
            // DiscoverDevices() в 32feet делает живой inquiry (включая неизвестные,
            // т.е. неспаренные устройства) с дефолтными параметрами.
            foreach (var d in cli.DiscoverDevices()) {
                res.Add((string.IsNullOrEmpty(d.DeviceName) ? d.DeviceAddress.ToString() : d.DeviceName,
                         d.DeviceAddress));
            }
            return res;
        }

        public bool Connect(BluetoothAddress addr) {
            try {
                Close();
                _cli = new BluetoothClient();
                _cli.Connect(new BluetoothEndPoint(addr, BluetoothService.SerialPort));
                _stream = _cli.GetStream();
                _rx = new Thread(RxLoop) { IsBackground = true };
                _rx.Start();
                Log?.Invoke($"Подключено к {addr}");
                return true;
            } catch (Exception ex) {
                Log?.Invoke("Ошибка подключения: " + ex.Message);
                return false;
            }
        }

        private void RxLoop() {
            var buf = new byte[512];
            var sb = new StringBuilder();
            try {
                while (_stream != null) {
                    int n = _stream.Read(buf, 0, buf.Length);
                    if (n <= 0) break;
                    for (int i = 0; i < n; i++) {
                        char c = (char)buf[i];
                        if (c == '\n' || c == '\r') {
                            if (sb.Length > 0) { Line?.Invoke(sb.ToString()); sb.Clear(); }
                        } else {
                            sb.Append(c);
                        }
                    }
                }
            } catch { /* отключение */ }
            Log?.Invoke("Соединение закрыто");
        }

        public void Send(byte[] data) {
            if (_stream == null) throw new InvalidOperationException("Нет соединения");
            _stream.Write(data, 0, data.Length);
        }

        public void SendLine(string s) => Send(Encoding.UTF8.GetBytes(s + "\n"));

        public void Close() {
            try { _stream?.Close(); } catch { }
            try { _cli?.Close(); } catch { }
            _stream = null;
            _cli = null;
        }

        public void Dispose() => Close();
    }
}
