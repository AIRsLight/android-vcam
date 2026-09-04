package io.github.androidvcam.backend;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.URI;
import java.net.URL;
import javax.net.ssl.HttpsURLConnection;

/** Small app_process entry point that reuses Android's platform TLS stack. */
public final class HttpsDownloader {
    private static final int CONNECT_TIMEOUT_MS = 15_000;
    private static final int READ_TIMEOUT_MS = 15_000;
    private static final int MAX_REDIRECTS = 5;
    private static final long MAX_BYTES = 2L * 1024L * 1024L * 1024L;
    private static final String OUTPUT_PREFIX = "/data/vendor/camera/vcam/providers/";

    private HttpsDownloader() {}

    public static void main(String[] args) {
        if (args.length != 2) {
            System.err.println("usage: HttpsDownloader HTTPS_URL OUTPUT_PATH");
            System.exit(64);
        }
        try {
            URL source = requireHttps(args[0]);
            File output = requireOutput(args[1]);
            download(source, output);
        } catch (Exception error) {
            System.err.println("HTTPS download failed: "
                    + error.getClass().getSimpleName() + ": " + safeMessage(error));
            System.exit(69);
        }
    }

    private static URL requireHttps(String value) throws Exception {
        URI uri = new URI(value);
        if (!"https".equalsIgnoreCase(uri.getScheme()) || uri.getHost() == null) {
            throw new IllegalArgumentException("an absolute HTTPS URL is required");
        }
        return uri.toURL();
    }

    private static File requireOutput(String value) throws IOException {
        File output = new File(value).getCanonicalFile();
        if (!output.getPath().startsWith(OUTPUT_PREFIX)) {
            throw new IllegalArgumentException("output must be in the VCAM provider directory");
        }
        File parent = output.getParentFile();
        if (parent == null || !parent.isDirectory()) {
            throw new IllegalArgumentException("output parent does not exist");
        }
        return output;
    }

    private static void download(URL initial, File output) throws Exception {
        URL current = initial;
        for (int redirects = 0; redirects <= MAX_REDIRECTS; redirects++) {
            HttpsURLConnection connection = (HttpsURLConnection) current.openConnection();
            connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
            connection.setReadTimeout(READ_TIMEOUT_MS);
            connection.setInstanceFollowRedirects(false);
            connection.setRequestProperty("User-Agent", "AndroidVcam/0.5");
            try {
                int status = connection.getResponseCode();
                if (isRedirect(status)) {
                    if (redirects == MAX_REDIRECTS) {
                        throw new IOException("too many redirects");
                    }
                    String location = connection.getHeaderField("Location");
                    if (location == null) throw new IOException("redirect has no location");
                    current = requireHttps(new URL(current, location).toString());
                    continue;
                }
                if (status < 200 || status >= 300) {
                    throw new IOException("server returned HTTP " + status);
                }
                long announced = connection.getContentLengthLong();
                if (announced > MAX_BYTES) throw new IOException("remote file is too large");
                copyBounded(connection.getInputStream(), output);
                return;
            } finally {
                connection.disconnect();
            }
        }
        throw new IOException("redirect handling failed");
    }

    private static boolean isRedirect(int status) {
        return status == 301 || status == 302 || status == 303
                || status == 307 || status == 308;
    }

    private static void copyBounded(InputStream source, File output) throws IOException {
        long total = 0;
        byte[] buffer = new byte[64 * 1024];
        try (BufferedInputStream input = new BufferedInputStream(source);
             FileOutputStream file = new FileOutputStream(output, false);
             BufferedOutputStream sink = new BufferedOutputStream(file)) {
            int count;
            while ((count = input.read(buffer)) != -1) {
                total += count;
                if (total > MAX_BYTES) throw new IOException("remote file is too large");
                sink.write(buffer, 0, count);
            }
            sink.flush();
            file.getFD().sync();
        } catch (IOException error) {
            output.delete();
            throw error;
        }
        if (total == 0) {
            output.delete();
            throw new IOException("remote file is empty");
        }
    }

    private static String safeMessage(Exception error) {
        String message = error.getMessage();
        if (message == null || message.isEmpty()) return "no details";
        return message.replace('\n', ' ').replace('\r', ' ');
    }
}
