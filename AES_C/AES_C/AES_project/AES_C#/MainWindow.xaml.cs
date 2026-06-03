using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Windows;
using Microsoft.Win32;

namespace AES_C_
{
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
        }

        // ───────────────────────────────────────────────
        // Chọn file dữ liệu đầu vào
        // ───────────────────────────────────────────────
        private void btnChonFileDauVao_Click(object sender, RoutedEventArgs e)
        {
            var dlg = new OpenFileDialog
            {
                Title = "Chọn file dữ liệu đầu vào",
                Filter = "Text files (*.txt)|*.txt|All files (*.*)|*.*"
            };

            if (dlg.ShowDialog() == true)
            {
                try
                {
                    txtInput.Text = File.ReadAllText(dlg.FileName, Encoding.UTF8);
                }
                catch (Exception ex)
                {
                    MessageBox.Show($"Không thể đọc file:\n{ex.Message}", "Lỗi",
                        MessageBoxButton.OK, MessageBoxImage.Error);
                }
            }
        }

        // ───────────────────────────────────────────────
        // Mở file khóa
        // ───────────────────────────────────────────────
        private void btnChonKhoaTuFile_Click(object sender, RoutedEventArgs e)
        {
            var dlg = new OpenFileDialog
            {
                Title = "Chọn file khóa AES",
                Filter = "Key files (*.key)|*.key|Text files (*.txt)|*.txt|All files (*.*)|*.*"
            };

            if (dlg.ShowDialog() == true)
            {
                try
                {
                    txtKey.Text = File.ReadAllText(dlg.FileName, Encoding.UTF8).Trim();
                }
                catch (Exception ex)
                {
                    MessageBox.Show($"Không thể đọc file khóa:\n{ex.Message}", "Lỗi",
                        MessageBoxButton.OK, MessageBoxImage.Error);
                }
            }
        }

        // ───────────────────────────────────────────────
        // Tự động sinh khóa ngẫu nhiên 256-bit (32 byte → Base64)
        // ───────────────────────────────────────────────
        private void btnSinhKhoaTuDong_Click(object sender, RoutedEventArgs e)
        {
            using var aes = Aes.Create();
            aes.KeySize = 256;
            aes.GenerateKey();
            txtKey.Text = Convert.ToBase64String(aes.Key);
            MessageBox.Show("Đã sinh khóa AES-256 ngẫu nhiên.\nHãy lưu khóa lại trước khi mã hóa!",
                "Sinh khóa thành công", MessageBoxButton.OK, MessageBoxImage.Information);
        }

        // ───────────────────────────────────────────────
        // Lưu khóa ra file
        // ───────────────────────────────────────────────
        private void btnLuuKhoaRaFile_Click(object sender, RoutedEventArgs e)
        {
            if (string.IsNullOrWhiteSpace(txtKey.Text))
            {
                MessageBox.Show("Chưa có khóa để lưu.", "Thông báo",
                    MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            var dlg = new SaveFileDialog
            {
                Title = "Lưu file khóa",
                Filter = "Key files (*.key)|*.key|Text files (*.txt)|*.txt",
                FileName = "aes_key"
            };

            if (dlg.ShowDialog() == true)
            {
                try
                {
                    File.WriteAllText(dlg.FileName, txtKey.Text.Trim(), Encoding.UTF8);
                    MessageBox.Show("Lưu khóa thành công!", "OK",
                        MessageBoxButton.OK, MessageBoxImage.Information);
                }
                catch (Exception ex)
                {
                    MessageBox.Show($"Không thể lưu file khóa:\n{ex.Message}", "Lỗi",
                        MessageBoxButton.OK, MessageBoxImage.Error);
                }
            }
        }

        // ───────────────────────────────────────────────
        // Mã hóa
        // ───────────────────────────────────────────────
        private void btnEncrypt_Click(object sender, RoutedEventArgs e)
        {
            if (!ValidateInputs(out string plaintext, out byte[] keyBytes)) return;

            try
            {
                string cipherBase64 = AesEncrypt(plaintext, keyBytes);
                txtOutput.Text = cipherBase64;
                MessageBox.Show("Mã hóa thành công!\nKết quả được hiển thị dưới dạng Base64.",
                    "Thành công", MessageBoxButton.OK, MessageBoxImage.Information);
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Lỗi khi mã hóa:\n{ex.Message}", "Lỗi",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        // ───────────────────────────────────────────────
        // Giải mã
        // ───────────────────────────────────────────────
        private void btnDecrypt_Click(object sender, RoutedEventArgs e)
        {
            if (!ValidateInputs(out string cipherBase64, out byte[] keyBytes)) return;

            try
            {
                string plaintext = AesDecrypt(cipherBase64, keyBytes);
                txtOutput.Text = plaintext;
                MessageBox.Show("Giải mã thành công!", "Thành công",
                    MessageBoxButton.OK, MessageBoxImage.Information);
            }
            catch (FormatException)
            {
                MessageBox.Show("Dữ liệu đầu vào không đúng định dạng Base64.\nHãy chắc chắn bạn đang dán đúng ciphertext.", "Lỗi định dạng",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
            catch (CryptographicException)
            {
                MessageBox.Show("Giải mã thất bại!\nKhóa sai hoặc dữ liệu bị hỏng.", "Lỗi giải mã",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Lỗi khi giải mã:\n{ex.Message}", "Lỗi",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        // ───────────────────────────────────────────────
        // Lưu kết quả ra file
        // ───────────────────────────────────────────────
        private void btnLuuFileKetQua_Click(object sender, RoutedEventArgs e)
        {
            if (string.IsNullOrWhiteSpace(txtOutput.Text))
            {
                MessageBox.Show("Chưa có kết quả để lưu.", "Thông báo",
                    MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            var dlg = new SaveFileDialog
            {
                Title = "Lưu kết quả",
                Filter = "Text files (*.txt)|*.txt|All files (*.*)|*.*",
                FileName = "result"
            };

            if (dlg.ShowDialog() == true)
            {
                try
                {
                    File.WriteAllText(dlg.FileName, txtOutput.Text, Encoding.UTF8);
                    MessageBox.Show("Lưu kết quả thành công!", "OK",
                        MessageBoxButton.OK, MessageBoxImage.Information);
                }
                catch (Exception ex)
                {
                    MessageBox.Show($"Không thể lưu file:\n{ex.Message}", "Lỗi",
                        MessageBoxButton.OK, MessageBoxImage.Error);
                }
            }
        }

        // ═══════════════════════════════════════════════
        // HELPER METHODS
        // ═══════════════════════════════════════════════

        /// <summary>
        /// Validate dữ liệu nhập và chuyển khóa về byte[].
        /// Khóa hỗ trợ: Base64 (ưu tiên) hoặc chuỗi UTF-8 (được hash SHA-256 thành 32 byte).
        /// </summary>
        private bool ValidateInputs(out string inputText, out byte[] keyBytes)
        {
            inputText = string.Empty;
            keyBytes = Array.Empty<byte>();

            if (string.IsNullOrWhiteSpace(txtInput.Text))
            {
                MessageBox.Show("Vui lòng nhập dữ liệu đầu vào.", "Thiếu dữ liệu",
                    MessageBoxButton.OK, MessageBoxImage.Warning);
                return false;
            }

            if (string.IsNullOrWhiteSpace(txtKey.Text))
            {
                MessageBox.Show("Vui lòng nhập hoặc sinh khóa bí mật.", "Thiếu khóa",
                    MessageBoxButton.OK, MessageBoxImage.Warning);
                return false;
            }

            inputText = txtInput.Text;

            // Thử parse Base64 trước; nếu không được thì hash SHA-256 chuỗi
            string keyStr = txtKey.Text.Trim();
            try
            {
                byte[] decoded = Convert.FromBase64String(keyStr);
                if (decoded.Length == 16 || decoded.Length == 24 || decoded.Length == 32)
                {
                    keyBytes = decoded;
                }
                else
                {
                    // Base64 hợp lệ nhưng độ dài sai → hash lại
                    keyBytes = SHA256.HashData(Encoding.UTF8.GetBytes(keyStr));
                }
            }
            catch
            {
                // Không phải Base64 → hash SHA-256 để có đúng 32 byte
                keyBytes = SHA256.HashData(Encoding.UTF8.GetBytes(keyStr));
            }

            return true;
        }

        /// <summary>
        /// AES-256-CBC encrypt. IV được sinh ngẫu nhiên và gắn vào đầu ciphertext.
        /// Output: Base64(IV[16 bytes] + CipherText)
        /// </summary>
        private static string AesEncrypt(string plaintext, byte[] key)
        {
            using var aes = Aes.Create();
            aes.Key = key;
            aes.Mode = CipherMode.CBC;
            aes.Padding = PaddingMode.PKCS7;
            aes.GenerateIV();

            byte[] iv = aes.IV;
            byte[] plaintextBytes = Encoding.UTF8.GetBytes(plaintext);

            using var encryptor = aes.CreateEncryptor();
            using var ms = new MemoryStream();
            ms.Write(iv, 0, iv.Length); // gắn IV vào đầu
            using (var cs = new CryptoStream(ms, encryptor, CryptoStreamMode.Write))
            {
                cs.Write(plaintextBytes, 0, plaintextBytes.Length);
                cs.FlushFinalBlock();
            }

            return Convert.ToBase64String(ms.ToArray());
        }

        /// <summary>
        /// AES-256-CBC decrypt. Đọc IV từ 16 byte đầu của Base64 input.
        /// </summary>
        private static string AesDecrypt(string cipherBase64, byte[] key)
        {
            byte[] fullData = Convert.FromBase64String(cipherBase64);

            if (fullData.Length < 16)
                throw new CryptographicException("Dữ liệu quá ngắn, không thể giải mã.");

            byte[] iv = fullData[..16];
            byte[] cipherBytes = fullData[16..];

            using var aes = Aes.Create();
            aes.Key = key;
            aes.IV = iv;
            aes.Mode = CipherMode.CBC;
            aes.Padding = PaddingMode.PKCS7;

            using var decryptor = aes.CreateDecryptor();
            using var ms = new MemoryStream(cipherBytes);
            using var cs = new CryptoStream(ms, decryptor, CryptoStreamMode.Read);
            using var reader = new StreamReader(cs, Encoding.UTF8);
            return reader.ReadToEnd();
        }
    }
}
