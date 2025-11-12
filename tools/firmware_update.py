import serial
import time
import os
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import threading


class SimpleYModemSender:
    def __init__(self):
        self.serial_port = None
        self.is_cancelled = False

        # Ymodem协议定义
        self.SOH = 0x01
        self.STX = 0x02
        self.EOT = 0x04
        self.ACK = 0x06
        self.NAK = 0x15
        self.CA = 0x18
        self.CRC16 = 0x43  # 'C'

    def open_serial(self, port, baudrate=115200):
        """初始化串口连接"""
        try:
            self.serial_port = serial.Serial(
                port=port,
                baudrate=baudrate,
                timeout=1
            )
            time.sleep(2)  # 等待串口稳定
            self.is_cancelled = False
            return True
        except Exception as e:
            print(f"串口打开失败: {e}")
            return False

    def close_serial(self):
        """关闭串口连接"""
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()

    def send_byte(self, byte_data):
        """发送单字节数据"""
        if self.serial_port and not self.is_cancelled:
            self.serial_port.write(bytes([byte_data]))

    def send_data(self, data):
        """发送数据块"""
        if self.serial_port and not self.is_cancelled:
            self.serial_port.write(data)

    def receive_byte(self, timeout=3):
        """接收单字节数据"""
        if self.serial_port and not self.is_cancelled:
            self.serial_port.timeout = timeout
            data = self.serial_port.read(1)
            return data[0] if data else None
        return None

    def calculate_crc(self, data):
        """计算CRC16校验值"""
        crc = 0x0000
        for byte in data:
            crc ^= byte << 8
            for _ in range(8):
                if crc & 0x8000:
                    crc = (crc << 1) ^ 0x1021
                else:
                    crc <<= 1
                crc &= 0xFFFF
        return crc

    def wait_for_sync(self, timeout=10, log_callback=None):
        """等待设备同步信号"""
        if log_callback:
            log_callback("第一阶段：等待设备同步...")

        start_time = time.time()
        sync_count = 0

        while time.time() - start_time < timeout and not self.is_cancelled:
            byte_recv = self.receive_byte(1)
            if byte_recv == self.CRC16:
                sync_count += 1
                if log_callback:
                    log_callback(f"收到同步字符'C' (第{sync_count}次)")

                # 检查是否有连续同步信号
                time.sleep(0.1)
                additional_c = self.receive_byte(0.1)
                if additional_c == self.CRC16:
                    sync_count += 1
                    if log_callback:
                        log_callback(f"收到额外同步字符'C' (共{sync_count}次)")

                if log_callback:
                    log_callback(f"同步完成，共收到 {sync_count} 个'C'")
                return True

            if log_callback and int(time.time() - start_time) % 3 == 0 and int(time.time() - start_time) > 0:
                log_callback(f"等待同步中... 已等待{int(time.time() - start_time)}秒")

            time.sleep(0.05)

        if self.is_cancelled:
            if log_callback:
                log_callback("同步过程被用户取消")
        else:
            if log_callback:
                log_callback("等待同步超时")
        return False

    def send_file_header(self, filename, file_size, log_callback=None):
        """发送文件头信息包"""
        if log_callback:
            log_callback("第二阶段：准备文件头包...")

        # 构建文件头数据包
        header = bytearray(133)  # 3字节头 + 128字节数据 + 2字节CRC
        header[0] = self.SOH
        header[1] = 0x00  # 包序号
        header[2] = 0xFF  # 包序号取反

        # 填充文件名和大小信息
        data_index = 3
        filename_bytes = filename.encode('ascii', 'ignore')

        if log_callback:
            log_callback(f"文件头信息 - 文件名: {filename}, 大小: {file_size} 字节")

        # 文件名填充
        for byte in filename_bytes:
            header[data_index] = byte
            data_index += 1
        header[data_index] = 0x00  # 文件名结束符
        data_index += 1

        # 文件大小信息
        size_str = str(file_size)
        for char in size_str:
            header[data_index] = ord(char)
            data_index += 1
        header[data_index] = 0x00  # 大小结束符

        # 剩余空间补零
        while data_index < 131:
            header[data_index] = 0x00
            data_index += 1

        # 计算CRC校验
        crc = self.calculate_crc(header[3:131])
        header[131] = (crc >> 8) & 0xFF
        header[132] = crc & 0xFF

        if log_callback:
            log_callback("发送文件头包...")

        self.send_data(header)

        # 等待确认响应
        if log_callback:
            log_callback("等待ACK响应...")

        ack_retry = 0
        while ack_retry < 5 and not self.is_cancelled:
            ack = self.receive_byte(3)
            if ack == self.ACK:
                if log_callback:
                    log_callback("文件头ACK确认收到")

                # 等待数据传输启动信号
                if log_callback:
                    log_callback("等待第二个'C'启动数据包传输...")

                second_c = self.receive_byte(3)
                if second_c == self.CRC16:
                    if log_callback:
                        log_callback("第二个'C'收到，文件头发送成功")
                    return True
                else:
                    if log_callback:
                        log_callback(f"第二个'C'等待失败，收到: 0x{second_c:02X if second_c else '无响应'}")
                    break
            elif ack is None:
                ack_retry += 1
                if log_callback:
                    log_callback(f"ACK等待超时，重试 {ack_retry}/5")
            else:
                if log_callback:
                    log_callback(f"意外响应: 0x{ack:02X}")
                break

        if log_callback:
            log_callback("文件头发送失败")
        return False

    def send_data_packet(self, packet_num, data, log_callback=None):
        """发送数据包"""
        if self.is_cancelled:
            return False

        packet_size = len(data)
        is_1k = packet_size == 1024

        if log_callback:
            log_callback(f"发送数据包 {packet_num}, 大小: {packet_size} 字节")

        # 构建数据包
        if is_1k:
            packet = bytearray(1029)  # STX + 序号 + 取反 + 1024数据 + 2字节CRC
            packet[0] = self.STX
            packet_size_to_send = 1024
        else:
            packet = bytearray(133)   # SOH + 序号 + 取反 + 128数据 + 2字节CRC
            packet[0] = self.SOH
            packet_size_to_send = 128
            # 数据包补齐
            if packet_size < 128:
                data = data + bytes([0x1A] * (128 - packet_size))
                if log_callback:
                    log_callback(f"数据包补全至128字节")

        packet[1] = packet_num & 0xFF
        packet[2] = (~packet_num) & 0xFF

        # 数据填充
        packet[3:3+packet_size_to_send] = data[:packet_size_to_send]

        # CRC计算
        crc = self.calculate_crc(packet[3:3+packet_size_to_send])
        packet[3+packet_size_to_send] = (crc >> 8) & 0xFF
        packet[3+packet_size_to_send+1] = crc & 0xFF

        # 发送数据包
        self.send_data(packet)

        # 等待确认
        ack_retry = 0
        while ack_retry < 3 and not self.is_cancelled:
            ack = self.receive_byte(10)
            if ack == self.ACK:
                if log_callback:
                    log_callback(f"数据包 {packet_num} 发送成功")
                return True
            elif ack == self.NAK:
                if log_callback:
                    log_callback(f"数据包 {packet_num} 被拒绝(NAK)，准备重发")
                return False
            elif ack is None:
                ack_retry += 1
                if log_callback:
                    log_callback(f"数据包 {packet_num} ACK等待超时，重试 {ack_retry}/3")
            else:
                if log_callback:
                    log_callback(f"数据包 {packet_num} 意外响应: 0x{ack:02X}")
                ack_retry += 1

        if log_callback:
            log_callback(f"数据包 {packet_num} 发送失败")
        return False

    def reset_transfer_state(self, log_callback=None):
        """重置传输状态和清空串口缓冲区"""
        if self.serial_port and self.serial_port.is_open:
            # 清空输入缓冲区
            self.serial_port.reset_input_buffer()
            # 清空输出缓冲区
            self.serial_port.reset_output_buffer()
            if log_callback:
                log_callback("串口缓冲区已清空")

        # 重置取消标志
        self.is_cancelled = False

    def send_file(self, file_path, progress_callback=None, log_callback=None):
        """主文件发送流程"""
        try:
            # 在开始传输前重置状态和清空缓冲区
            self.reset_transfer_state(log_callback)

            if self.is_cancelled:
                return False, "传输被用户取消"

            if not os.path.exists(file_path):
                return False, "文件不存在"

            file_size = os.path.getsize(file_path)
            filename = os.path.basename(file_path)

            if log_callback:
                log_callback("=" * 50)
                log_callback(f"YModem文件传输启动")
                log_callback(f"文件: {filename}")
                log_callback(f"大小: {file_size} 字节")
                log_callback("=" * 50)

            # 第一阶段：设备同步
            if not self.wait_for_sync(timeout=15, log_callback=log_callback):
                return False, "设备同步失败"

            if self.is_cancelled:
                return False, "传输被用户取消"

            # 第二阶段：文件头发送
            if not self.send_file_header(filename, file_size, log_callback):
                return False, "文件头发送失败"

            if self.is_cancelled:
                return False, "传输被用户取消"

            # 第三阶段：数据传输
            if log_callback:
                log_callback("第三阶段：数据传输开始...")

            packet_num = 1
            bytes_sent = 0

            with open(file_path, 'rb') as file:
                while not self.is_cancelled and bytes_sent < file_size:
                    # 根据剩余字节数决定读取大小
                    remaining = file_size - bytes_sent

                    if remaining >= 1024:
                        # 读取1024字节使用STX包
                        data = file.read(1024)
                    else:
                        # 剩余不足1024字节，按128字节读取使用SOH包
                        read_size = min(128, remaining)
                        data = file.read(read_size)

                    if not data:
                        break

                    bytes_sent += len(data)

                    # 进度更新
                    if progress_callback:
                        progress = min(100, int((bytes_sent / file_size) * 100))
                        progress_callback(progress, packet_num, bytes_sent, file_size)

                    if log_callback and packet_num % 10 == 0:
                        log_callback(f"传输进度: {progress}% ({bytes_sent}/{file_size} 字节)")

                    # 数据包发送（最大重试3次）
                    success = False
                    for retry in range(3):
                        if self.send_data_packet(packet_num, data, log_callback):
                            success = True
                            break
                        elif self.is_cancelled:
                            break
                        else:
                            if log_callback:
                                log_callback(f"数据包 {packet_num} 第 {retry + 1} 次重试")

                    if not success:
                        return False, f"数据包 {packet_num} 发送失败"

                    packet_num += 1

            if self.is_cancelled:
                return False, "传输被用户取消"

            # 第四阶段：传输结束
            if log_callback:
                log_callback("第四阶段：发送结束标志(EOT)...")

            self.send_byte(self.EOT)

            # 等待结束确认
            if log_callback:
                log_callback("等待NAK响应...")

            nak_received = False
            for retry in range(5):
                response = self.receive_byte(3)
                if response == self.NAK:
                    nak_received = True
                    if log_callback:
                        log_callback("收到NAK，发送第二个EOT")
                    break
                elif response is not None:
                    if log_callback:
                        log_callback(f"收到响应: 0x{response:02X}")
                else:
                    if log_callback:
                        log_callback(f"NAK等待超时，重试 {retry + 1}/5")

            self.send_byte(self.EOT)

            if log_callback:
                log_callback("等待传输结束确认...")

            ack_received = False
            for retry in range(10):
                response = self.receive_byte(3)
                if response == self.ACK:
                    ack_received = True
                    if log_callback:
                        log_callback("收到结束确认ACK")
                    break
                elif response is not None:
                    if log_callback:
                        log_callback(f"收到响应: 0x{response:02X}，继续等待ACK...")
                else:
                    if log_callback and retry % 2 == 0:
                        log_callback(f"ACK等待超时，重试 {retry + 1}/10")

            if ack_received:
                if log_callback:
                    log_callback("=" * 50)
                    log_callback("文件传输完成！")
                    log_callback("=" * 50)
                return True, "文件发送成功"
            else:
                if log_callback:
                    log_callback("结束确认超时，数据包已全部发送")
                return True, "文件发送完成（结束确认超时）"

        except Exception as e:
            if log_callback:
                log_callback(f"发送过程出错: {str(e)}")
            return False, f"发送过程出错: {str(e)}"

    def cancel_transfer(self):
        """取消当前传输"""
        self.is_cancelled = True
        if self.serial_port and self.serial_port.is_open:
            self.send_byte(self.CA)
            self.send_byte(self.CA)


class FirmwareUpdater:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("固件更新工具")
        self.root.geometry("700x550")

        self.ymodem = SimpleYModemSender()
        self.update_thread = None
        self.is_serial_open = False

        self.setup_ui()

    def setup_ui(self):
        """初始化用户界面"""
        # 主框架
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.pack(fill=tk.BOTH, expand=True)

        # 标题区域
        title_label = ttk.Label(main_frame, text="BOOTLOADER 固件更新工具",
                               font=("Arial", 12, "bold"), foreground="blue")
        title_label.pack(pady=10)

        # 串口设置区域
        port_frame = ttk.LabelFrame(main_frame, text="串口设置", padding="5")
        port_frame.pack(fill=tk.X, pady=5)

        port_row = ttk.Frame(port_frame)
        port_row.pack(fill=tk.X)

        ttk.Label(port_row, text="串口:").pack(side=tk.LEFT)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(port_row, textvariable=self.port_var, width=15)
        self.port_combo.pack(side=tk.LEFT, padx=5)

        ttk.Button(port_row, text="刷新端口", command=self.scan_ports).pack(side=tk.LEFT, padx=5)

        ttk.Label(port_row, text="波特率:").pack(side=tk.LEFT, padx=(20, 0))
        self.baud_var = tk.StringVar(value="115200")
        baud_combo = ttk.Combobox(port_row, textvariable=self.baud_var,
                                 values=["9600", "19200", "38400", "57600", "115200"], width=10)
        baud_combo.pack(side=tk.LEFT, padx=5)

        # 串口控制按钮
        btn_frame = ttk.Frame(port_frame)
        btn_frame.pack(fill=tk.X, pady=5)

        self.open_btn = ttk.Button(btn_frame, text="打开串口", command=self.open_serial)
        self.open_btn.pack(side=tk.LEFT, padx=5)

        self.close_btn = ttk.Button(btn_frame, text="关闭串口", command=self.close_serial, state=tk.DISABLED)
        self.close_btn.pack(side=tk.LEFT, padx=5)

        self.status_label = ttk.Label(btn_frame, text="● 串口未打开", foreground="red",
                                     font=("Arial", 9, "bold"))
        self.status_label.pack(side=tk.LEFT, padx=20)

        # 文件选择区域
        file_frame = ttk.LabelFrame(main_frame, text="固件文件", padding="5")
        file_frame.pack(fill=tk.X, pady=5)

        file_row = ttk.Frame(file_frame)
        file_row.pack(fill=tk.X)

        ttk.Label(file_row, text="文件路径:").pack(side=tk.LEFT)
        self.file_var = tk.StringVar()
        file_entry = ttk.Entry(file_row, textvariable=self.file_var, width=50)
        file_entry.pack(side=tk.LEFT, padx=5, fill=tk.X, expand=True)

        ttk.Button(file_row, text="浏览", command=self.browse_file).pack(side=tk.LEFT, padx=5)

        # 进度显示区域
        progress_frame = ttk.LabelFrame(main_frame, text="传输进度", padding="5")
        progress_frame.pack(fill=tk.X, pady=5)

        self.progress_bar = ttk.Progressbar(progress_frame, mode='determinate')
        self.progress_bar.pack(fill=tk.X, pady=2)

        self.progress_text = ttk.Label(progress_frame, text="就绪")
        self.progress_text.pack()

        # 日志显示区域
        log_frame = ttk.LabelFrame(main_frame, text="通信日志", padding="5")
        log_frame.pack(fill=tk.BOTH, expand=True, pady=5)

        self.log_text = tk.Text(log_frame, height=15, wrap=tk.WORD)
        scrollbar = ttk.Scrollbar(log_frame, command=self.log_text.yview)
        self.log_text.config(yscrollcommand=scrollbar.set)

        self.log_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        # 控制按钮区域
        control_frame = ttk.Frame(main_frame)
        control_frame.pack(fill=tk.X, pady=10)

        self.start_btn = ttk.Button(control_frame, text="开始更新", command=self.start_update, state=tk.DISABLED)
        self.start_btn.pack(side=tk.LEFT, padx=5)

        self.cancel_btn = ttk.Button(control_frame, text="停止更新", command=self.cancel_update, state=tk.DISABLED)
        self.cancel_btn.pack(side=tk.LEFT, padx=5)

        ttk.Button(control_frame, text="清除日志", command=self.clear_log).pack(side=tk.LEFT, padx=5)
        ttk.Button(control_frame, text="退出", command=self.quit_app).pack(side=tk.LEFT, padx=5)

        # 初始端口扫描
        self.scan_ports()

    def scan_ports(self):
        """扫描可用串口设备"""
        if self.is_serial_open:
            messagebox.showwarning("警告", "请先关闭串口再刷新端口")
            return

        try:
            from serial.tools import list_ports
            ports = [port.device for port in list_ports.comports()]
            self.port_combo['values'] = ports
            if ports:
                self.port_var.set(ports[0])
            self.log_message(f"发现 {len(ports)} 个串口设备")
        except ImportError:
            self.log_message("警告: 串口自动扫描不可用，请手动输入端口号")

    def open_serial(self):
        """建立串口连接"""
        if not self.port_var.get():
            messagebox.showerror("错误", "请选择串口设备")
            return

        self.log_message(f"打开串口: {self.port_var.get()} @ {self.baud_var.get()} bps")

        if self.ymodem.open_serial(self.port_var.get(), int(self.baud_var.get())):
            self.is_serial_open = True
            self.open_btn.config(state=tk.DISABLED)
            self.close_btn.config(state=tk.NORMAL)
            self.start_btn.config(state=tk.NORMAL)
            self.status_label.config(text="● 串口已打开", foreground="green")
            self.log_message("串口打开成功")
        else:
            messagebox.showerror("错误", "串口打开失败")

    def close_serial(self):
        """关闭串口连接"""
        self.ymodem.close_serial()
        self.is_serial_open = False
        self.open_btn.config(state=tk.NORMAL)
        self.close_btn.config(state=tk.DISABLED)
        self.start_btn.config(state=tk.DISABLED)
        self.status_label.config(text="● 串口未打开", foreground="red")
        self.log_message("串口已关闭")

    def browse_file(self):
        """选择固件文件"""
        filename = filedialog.askopenfilename(
            title="选择固件文件",
            filetypes=[("固件文件", "*.bin *.hex"), ("所有文件", "*.*")]
        )
        if filename:
            self.file_var.set(filename)
            size = os.path.getsize(filename)
            self.log_message(f"选择文件: {os.path.basename(filename)}, 大小: {size} 字节")

    def log_message(self, message):
        """添加日志信息"""
        timestamp = time.strftime('%H:%M:%S')
        self.log_text.insert(tk.END, f"[{timestamp}] {message}\n")
        self.log_text.see(tk.END)
        self.root.update()
        print(f"[{timestamp}] {message}")

    def clear_log(self):
        """清空日志内容"""
        self.log_text.delete(1.0, tk.END)

    def update_progress(self, progress, packet_num, current, total):
        """更新传输进度"""
        self.progress_bar['value'] = progress
        self.progress_text.config(text=f"进度: {progress}% | 包: {packet_num} | {current}/{total} 字节")
        self.root.update()

    def start_update(self):
        """启动固件更新流程"""
        if not self.file_var.get():
            messagebox.showerror("错误", "请选择固件文件")
            return

        self.start_btn.config(state=tk.DISABLED)
        self.cancel_btn.config(state=tk.NORMAL)
        self.open_btn.config(state=tk.DISABLED)
        self.close_btn.config(state=tk.DISABLED)

        # 重置进度状态
        self.progress_bar['value'] = 0
        self.progress_text.config(text="开始传输...")

        # 启动单次更新模式
        self.update_thread = threading.Thread(target=self._do_update)
        self.update_thread.daemon = True
        self.update_thread.start()

    def cancel_update(self):
        """取消/停止更新操作"""
        self.ymodem.cancel_transfer()
        self.cancel_btn.config(state=tk.DISABLED)
        self.log_message("用户停止更新...")

    def _do_update(self):
        """执行单次更新操作"""
        try:
            self.log_message("固件更新流程启动...")

            success, message = self._execute_single_update()

            if success:
                self.log_message("固件更新成功！")
                self.progress_bar['value'] = 100
                self.progress_text.config(text="传输完成！")
                messagebox.showinfo("成功", "固件更新完成！")
            else:
                self.log_message(f"更新失败: {message}")
                messagebox.showerror("错误", f"更新失败: {message}")

        except Exception as e:
            self.log_message(f"更新过程出错: {str(e)}")
            messagebox.showerror("错误", f"更新过程出错: {str(e)}")
        finally:
            self._reset_buttons()

    def _execute_single_update(self):
        """执行一次完整的更新流程"""
        # 重置取消标志
        self.ymodem.is_cancelled = False

        # 重置进度
        self.root.after(0, lambda: self.progress_bar.config(value=0))
        self.root.after(0, lambda: self.progress_text.config(text="正在传输..."))

        # 执行传输
        success, message = self.ymodem.send_file(
            self.file_var.get(),
            progress_callback=lambda p, pn, c, t: self.root.after(0, lambda: self.update_progress(p, pn, c, t)),
            log_callback=lambda m: self.root.after(0, lambda msg=m: self.log_message(msg))
        )

        return success, message

    def _reset_buttons(self):
        """重置按钮状态"""
        self.start_btn.config(state=tk.NORMAL if self.is_serial_open else tk.DISABLED)
        self.cancel_btn.config(state=tk.DISABLED)
        self.open_btn.config(state=tk.NORMAL if not self.is_serial_open else tk.DISABLED)
        self.close_btn.config(state=tk.NORMAL if self.is_serial_open else tk.DISABLED)
        self.loop_check.config(state=tk.NORMAL)

    def quit_app(self):
        """退出应用程序"""
        if self.loop_enabled:
            result = messagebox.askyesno("确认", "循环更新正在进行中，确定要退出吗？")
            if not result:
                return
            self.cancel_update()
            time.sleep(0.5)

        if self.is_serial_open:
            self.close_serial()

        self.root.quit()

    def run(self):
        """启动应用程序"""
        self.root.mainloop()


def main():
    app = FirmwareUpdater()
    app.run()


if __name__ == "__main__":
    main()
