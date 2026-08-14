package io.github.androidvcam.manager;

import android.net.LocalSocket;
import android.net.LocalSocketAddress;

import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

final class BackendClient {
    private static final String SOCKET = "android_vcam_control";
    private static final byte[] REQUEST_MAGIC = "VCAMD001".getBytes(StandardCharsets.US_ASCII);
    private static final byte[] RESPONSE_MAGIC = "VCAMR001".getBytes(StandardCharsets.US_ASCII);
    private static final int MAX_OUTPUT = 4 * 1024 * 1024;

    static class Result {
        final int code;
        final String output;

        Result(int code, String output) {
            this.code = code;
            this.output = output == null ? "" : output.trim();
        }

        void requireSuccess() throws IOException {
            if (code != 0) throw new IOException(output.isEmpty() ? "后端操作失败：" + code : output);
        }
    }

    private BackendClient() { }

    static Result controller(String... arguments) throws IOException {
        return request(null, 0, arguments);
    }

    static Result controller(byte[] input, String... arguments) throws IOException {
        return request(new ByteArrayInputStream(input), input.length, arguments);
    }

    static Result controller(InputStream input, long length, String... arguments) throws IOException {
        if (input == null || length < 0) throw new IOException("无效的媒体输入");
        return request(input, length, arguments);
    }

    private static Result request(InputStream payload, long payloadLength, String... values)
            throws IOException {
        if (values.length == 0) throw new IOException("缺少后端命令");
        byte[] command = values[0].getBytes(StandardCharsets.UTF_8);
        LocalSocket socket = new LocalSocket();
        try {
            try {
                socket.connect(new LocalSocketAddress(SOCKET, LocalSocketAddress.Namespace.ABSTRACT));
            } catch (IOException error) {
                throw new IOException("无法连接虚拟摄像头后端，请确认模块已启用", error);
            }
            socket.setSoTimeout(30_000);
            DataOutputStream output = new DataOutputStream(socket.getOutputStream());
            output.write(REQUEST_MAGIC);
            output.writeInt(command.length);
            output.writeInt(values.length - 1);
            output.writeLong(payloadLength);
            output.write(command);
            for (int index = 1; index < values.length; ++index) {
                byte[] argument = values[index].getBytes(StandardCharsets.UTF_8);
                output.writeInt(argument.length);
                output.write(argument);
            }
            if (payload != null) {
                byte[] buffer = new byte[64 * 1024];
                long remaining = payloadLength;
                while (remaining > 0) {
                    int wanted = (int)Math.min(buffer.length, remaining);
                    int count = payload.read(buffer, 0, wanted);
                    if (count < 0) throw new IOException("媒体输入提前结束");
                    output.write(buffer, 0, count);
                    remaining -= count;
                }
            }
            output.flush();

            DataInputStream response = new DataInputStream(socket.getInputStream());
            byte[] magic = new byte[8];
            response.readFully(magic);
            if (!java.util.Arrays.equals(magic, RESPONSE_MAGIC)) throw new IOException("后端响应无效");
            int code = response.readInt();
            int length = response.readInt();
            if (length < 0 || length > MAX_OUTPUT) throw new IOException("后端响应过大");
            byte[] bytes = new byte[length];
            response.readFully(bytes);
            return new Result(code, new String(bytes, StandardCharsets.UTF_8));
        } finally {
            socket.close();
        }
    }

    static String encode(String value) {
        return android.util.Base64.encodeToString(
                value.getBytes(StandardCharsets.UTF_8), android.util.Base64.NO_WRAP);
    }

    static String decode(String value) {
        if (value == null || value.isEmpty()) return "";
        try {
            return new String(android.util.Base64.decode(value, android.util.Base64.DEFAULT),
                    StandardCharsets.UTF_8);
        } catch (IllegalArgumentException ignored) { return value; }
    }
}
