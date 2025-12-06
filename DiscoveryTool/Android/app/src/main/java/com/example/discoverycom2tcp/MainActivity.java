package com.example.discoverycom2tcp;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.ProgressBar;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.SocketTimeoutException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import com.example.discoverycom2tcp.udp.UdpTransceiver;

public class MainActivity extends AppCompatActivity {

    private static final String TAG = "COM2TCPDiscovery";
    private static final int SERVER_DISCOVERY_PORT = 19000;
    private static final String DISCOVERY_MAGIC = "DISCOVER_COM2TCP_SERVER";
    private static final int BUFFER_SIZE = 65507;

    private static final int OPTIMAL_TIMEOUT_MS = 1000;
    private static final int SOCKET_TIMEOUT_MS = 10;

    private Button searchButton;
    private Button stopButton;
    private TextView statusText;
    private Spinner serverSpinner;
    private TextView serverDetailsText;
    private ProgressBar progressBar;

    private final List<String> discoveredServers = new ArrayList<>();
    private final List<ServerInfo> serverInfoList = new ArrayList<>();
    private final Set<String> serverIds = new HashSet<>();
    private final Map<String, Integer> serverCountMap = new HashMap<>(); // 统计每个服务器收到多少次
    private ArrayAdapter<String> spinnerAdapter;

    private final AtomicBoolean isSearching = new AtomicBoolean(false);
    private final AtomicBoolean listening = new AtomicBoolean(false);
    private ExecutorService executorService;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    private DatagramSocket socket;
    private final AtomicInteger totalMessages = new AtomicInteger(0);
    private final AtomicInteger validMessages = new AtomicInteger(0);
    private final AtomicInteger duplicateMessages = new AtomicInteger(0);
    private final AtomicInteger uniqueServers = new AtomicInteger(0);
    private long searchStartTime;
    private UdpTransceiver udpTransceiver;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        initUdpTransceiver();
        initViews();
    }

    private void initUdpTransceiver() {
        udpTransceiver = new UdpTransceiver();
        udpTransceiver.setCallback(new UdpTransceiver.UdpCallback() {
            @Override
            public void onMessageReceived(String message, String sourceIp, int sourcePort) {
                // 实时处理每个接收到的消息
                Log.d(TAG, "收到UDP消息: " + sourceIp + ":" + sourcePort + " -> " +
                        (message.length() > 50 ? message.substring(0, 50) + "..." : message));

                // 业务逻辑：如果是COM2TCP服务器消息，则处理
                if (message.startsWith("COM2TCP_SERVER")) {
                    ServerInfo info = parseServerResponseWithDebug(message, sourceIp);
                    if (info != null) {
                        processServerInfo(info);
                    }
                }
            }

            @Override
            public void onReceiveComplete(List<UdpTransceiver.UdpMessage> messages) {
                runOnUiThread(() -> {
                    // 统计结果
                    int com2tcpServers = 0;
                    int otherMessages = 0;

                    for (UdpTransceiver.UdpMessage msg : messages) {
                        if (msg.message.startsWith("COM2TCP_SERVER")) {
                            com2tcpServers++;
                        } else {
                            otherMessages++;
                        }
                    }

                    // 更新UI
                    updateSpinnerList();

                    if (serverInfoList.isEmpty()) {
                        statusText.setText("未发现服务器");
                        serverDetailsText.setText("搜索完成，未发现任何COM2TCP服务器。");
                    } else {
                        statusText.setText("发现 " + serverInfoList.size() + " 个服务器");
                        serverDetailsText.setText(String.format(
                                "搜索完成\n发现服务器: %d个\n其他消息: %d条\n总消息: %d条",
                                com2tcpServers, otherMessages, messages.size()
                        ));
                    }

                    progressBar.setVisibility(View.GONE);
                    stopButton.setEnabled(false);
                    isSearching.set(false);
                });
            }

            @Override
            public void onError(Exception e) {
                runOnUiThread(() -> {
                    statusText.setText("通信失败");
                    serverDetailsText.setText("错误: " + e.getMessage());
                    progressBar.setVisibility(View.GONE);
                    stopButton.setEnabled(false);
                    isSearching.set(false);
                });
            }
        });
    }

    private void initViews() {
        searchButton = findViewById(R.id.searchButton);
        stopButton = findViewById(R.id.stopButton);
        statusText = findViewById(R.id.statusText);
        serverSpinner = findViewById(R.id.serverSpinner);
        serverDetailsText = findViewById(R.id.serverDetailsText);
        progressBar = findViewById(R.id.progressBar);

        discoveredServers.add("无服务器");
        spinnerAdapter = new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_item, discoveredServers);
        spinnerAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        serverSpinner.setAdapter(spinnerAdapter);

        searchButton.setOnClickListener(v -> startDiscovery());
        stopButton.setOnClickListener(v -> stopDiscovery());

        serverSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                if (position == 0 && discoveredServers.get(0).equals("无服务器")) {
                    serverDetailsText.setText("请先搜索服务器");
                } else if (position > 0 && position - 1 < serverInfoList.size()) {
                    ServerInfo info = serverInfoList.get(position - 1);
                    serverDetailsText.setText(info.getDetails());
                }
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
                serverDetailsText.setText("请选择一个服务器");
            }
        });
    }

    private void startDiscovery() {
        synchronized (isSearching) {
            if (isSearching.get()) {
                statusText.setText("搜索中...忽略点击");
                return;
            }
            isSearching.set(true);
        }

        // 重置状态
        clearServersSilently();

        runOnUiThread(() -> {
            stopButton.setEnabled(true);
            progressBar.setVisibility(View.VISIBLE);
            statusText.setText("搜索中...");
            serverDetailsText.setText("正在搜索COM2TCP服务器...");
        });

        // 使用纯粹的UDP收发器
        udpTransceiver.sendAndReceive(
                "255.255.255.255",    // 广播地址
                19000,                      // 端口
                "DISCOVER_COM2TCP_SERVER",  // 发送的消息
                200,                        // 超时时间2秒
                0                           // 随机监听端口
        );
    }

    private void performDiscovery() {
        try {
            clearServersSilently();
            Log.d(TAG, "=== 开始搜索 ===");
            searchStartTime = System.currentTimeMillis();

            createOptimizedSocket();
            if (socket == null) {
                throw new IOException("创建Socket失败");
            }

            sendBroadcast();
            receiveAndDebugMessages();

            long elapsedTime = System.currentTimeMillis() - searchStartTime;
            analyzeDedupResults();

            Log.d(TAG, "=== 搜索完成 ===");
            Log.d(TAG, "总耗时: " + elapsedTime + "ms");

            // 更新UI
            runOnUiThread(() -> {
                updateSpinnerList();

                if (serverInfoList.isEmpty()) {
                    statusText.setText("未发现服务器");
                    serverDetailsText.setText("搜索完成，未发现任何COM2TCP服务器。");
                } else {
                    statusText.setText("发现 " + serverInfoList.size() + " 个服务器，消息数: "
                             + uniqueServers.get() +"/"+ validMessages.get()
                            + "，重复：" + duplicateMessages.get());

                    StringBuilder stats = new StringBuilder();
                    stats.append("搜索完成 - 发现 ").append(serverInfoList.size()).append(" 个服务器\n");
                    stats.append("耗时: ").append(elapsedTime).append("ms\n");
                    serverDetailsText.setText(stats.toString());
                }
            });

        } catch (Exception e) {
            Log.e(TAG, "搜索出错: " + e.getMessage(), e);
            runOnUiThread(() -> {
                statusText.setText("搜索出错");
                serverDetailsText.setText("错误: " + e.getMessage());
            });
        } finally {

            // 关闭socket
            if (socket != null) {
                try {
                    socket.close();
                } catch (Exception e) {
                    Log.e(TAG, "关闭Socket失败", e);
                }
                socket = null;
            }

            runOnUiThread(() -> {
                stopButton.setEnabled(false);
                progressBar.setVisibility(View.GONE);
            });

            // 清除搜索标志
            listening.set(false);
            isSearching.set(false);
        }
    }

    private void createOptimizedSocket() {
        try {
            socket = new DatagramSocket();
            socket.setBroadcast(true);
            socket.setReuseAddress(true);
            socket.setSoTimeout(SOCKET_TIMEOUT_MS);

            try {
                socket.setReceiveBufferSize(512 * 1024);
            } catch (Exception e) {
                Log.d(TAG, "设置缓冲区失败: " + e.getMessage());
            }

        } catch (Exception e) {
            Log.e(TAG, "创建Socket失败: " + e.getMessage(), e);
            socket = null;
        }
    }

    private void sendBroadcast() {
        if (socket == null) return;

        try {
            byte[] sendData = DISCOVERY_MAGIC.getBytes();
            InetAddress broadcast = InetAddress.getByName("255.255.255.255");
            DatagramPacket packet = new DatagramPacket(sendData, sendData.length,
                    broadcast, SERVER_DISCOVERY_PORT);

            socket.send(packet);
            Log.d(TAG, "广播已发送");

        } catch (Exception e) {
            Log.e(TAG, "发送广播失败: " + e.getMessage(), e);
        }
    }

    private void receiveAndDebugMessages() {
        if (socket == null) return;

        byte[] buffer = new byte[BUFFER_SIZE];
        DatagramPacket packet = new DatagramPacket(buffer, buffer.length);

        long endTime = searchStartTime + OPTIMAL_TIMEOUT_MS;
        long lastUpdateTime = searchStartTime;

        Log.d(TAG, "开始接收消息...");

        // 用于调试：记录解析失败的响应
        List<String> failedResponses = new ArrayList<>();

        while (listening.get() && System.currentTimeMillis() < endTime) {
            try {
                long remainingTime = endTime - System.currentTimeMillis();
                if (remainingTime <= 0) break;

                int socketTimeout = (int) Math.min(remainingTime, SOCKET_TIMEOUT_MS);
                socket.setSoTimeout(socketTimeout);

                socket.receive(packet);
                totalMessages.incrementAndGet();

                String message = new String(packet.getData(), 0, packet.getLength());
                String sender = packet.getAddress().getHostAddress();

                if (message.startsWith("COM2TCP_SERVER")) {
                    validMessages.incrementAndGet();

                    // 解析服务器信息
                    ServerInfo info = parseServerResponseWithDebug(message, sender);
                    if (info != null) {
                        processServerInfo(info);
                    } else {
                        // 记录解析失败的响应（最多记录5个）
                        if (failedResponses.size() < 5) {
                            failedResponses.add("解析失败: " + message.substring(0, Math.min(message.length(), 100)));
                        }
                    }
                }

                // 更新UI
                long currentTime = System.currentTimeMillis();
                if (currentTime - lastUpdateTime >= 500) {
                    updateUIProgress(currentTime);
                    lastUpdateTime = currentTime;
                }

                packet.setLength(buffer.length);

            } catch (SocketTimeoutException e) {
                continue;
            } catch (IOException e) {
                if (listening.get()) {
                    Log.e(TAG, "接收错误: " + e.getMessage(), e);
                }
                break;
            }
        }

        // 输出解析失败的响应
        if (!failedResponses.isEmpty()) {
            Log.w(TAG, "解析失败的响应:");
            for (String failed : failedResponses) {
                Log.w(TAG, failed);
            }
        }
    }

    private ServerInfo parseServerResponseWithDebug(String response, String sender) {
        try {
            String[] parts = response.split("\\|", -1);

            if (parts.length >= 7) {
                ServerInfo info = new ServerInfo();

                // 索引0: COM2TCP_SERVER
                // 索引1: 服务器名称（可能带空格）
                String rawServerName = parts[1];

                // 去除所有空格（不仅是trim，而是替换所有空格）
                info.serverName = rawServerName.replace(" ", "");

                if (info.serverName.isEmpty())
                    info.serverName = rawServerName.trim();

                info.hostName = info.serverName;

                String rawIp = parts[2];
                info.ipAddress = rawIp.trim();  // IP地址只需要trim

                String rawPort = parts[3];
                info.port = rawPort.trim();

                info.clientCount = safeGet(parts, 4, "0").trim();
                info.maxClients = safeGet(parts, 5, "0").trim();
                String rawComputerId = parts[6];

                // 去除ID中可能的多余字符
                if (rawComputerId.endsWith("|")) {
                    info.computerId = rawComputerId.substring(0, rawComputerId.length() - 1).trim();
                } else {
                    info.computerId = rawComputerId.trim();
                }

                info.responseSource = sender;
                info.rawResponse = response;

                // 验证必要字段
                if (info.ipAddress.isEmpty() || info.port.isEmpty()) {
                    Log.w(TAG, "服务器信息不完整: IP=" + info.ipAddress + ", Port=" + info.port);
                    return null;
                }

                return info;
            } else {
                Log.w(TAG, "响应格式错误，期望至少7部分，实际: " + parts.length);
                Log.w(TAG, "响应内容: " + response);
                return null;
            }
        } catch (Exception e) {
            Log.e(TAG, "解析响应异常: " + e.getMessage());
            Log.e(TAG, "响应内容: " + response);
            return null;
        }
    }

    private String safeGet(String[] array, int index, String defaultValue) {
        if (array != null && index >= 0 && index < array.length) {
            String value = array[index].trim();
            return value.isEmpty() ? defaultValue : value;
        }
        return defaultValue;
    }

    private void processServerInfo(ServerInfo info) {
        // 生成更准确的唯一标识符
        String primaryId = info.ipAddress + ":" + info.port + ":" + info.computerId;
        String secondaryId = info.ipAddress + ":" + info.port + ":" + info.serverName;
        String fallbackId = info.ipAddress + ":" + info.port;

        synchronized (discoveredServers) {
            // 首先检查最准确的标识
            if (!serverIds.contains(primaryId)) {
                // 新服务器
                serverIds.add(primaryId);
                serverInfoList.add(info);
                uniqueServers.incrementAndGet();
                serverCountMap.put(primaryId, 1);

//                Log.d(TAG, "发现新服务器 #" + uniqueServers.get() + ": " +
//                        info.serverName + " (" + info.ipAddress + ":" + info.port +
//                        ") ID:" + info.computerId);
            } else {
                // 重复服务器
                duplicateMessages.incrementAndGet();

                // 更新统计
                serverCountMap.put(primaryId, serverCountMap.getOrDefault(primaryId, 0) + 1);

                Log.d(TAG, "重复消息: " + info.serverName + " (" + info.ipAddress + ":" + info.port + ")");
            }
        }
    }

    private void analyzeDedupResults() {
        Log.d(TAG, "=== 搜索结果 ===");
        Log.d(TAG, "消息数: " + uniqueServers.get() +"/"+ validMessages.get()
                + "，重复：" + duplicateMessages.get());

        // 计算平均每个服务器收到多少次
        if (uniqueServers.get() > 0) {
            double avgResponses = validMessages.get() / (double) uniqueServers.get();
            Log.d(TAG, "平均每个服务器响应次数: " + String.format("%.2f", avgResponses));
        }

        // 输出服务器统计
        if (!serverCountMap.isEmpty()) {
            Log.d(TAG, "服务器响应次数分布:");
//            for (Map.Entry<String, Integer> entry : serverCountMap.entrySet())
//                Log.d(TAG, "  " + entry.getKey() + ": " + entry.getValue() + " 次");
        }
    }

    private void updateUIProgress(long currentTime) {
        long elapsed = currentTime - searchStartTime;
        double progress = (elapsed * 100.0) / OPTIMAL_TIMEOUT_MS;

        mainHandler.post(() -> {
            progressBar.setProgress(Math.min((int) progress, 100));

            String text = String.format("搜索中... %.0f%%\n", Math.min(progress, 100)) +
                    "已用时: " + elapsed + "ms\n" +
                    "总消息: " + totalMessages.get() + "\n" +
                    "有效消息: " + validMessages.get() + "\n" +
                    "重复消息: " + duplicateMessages.get() + "\n" +
                    "唯一服务器: " + uniqueServers.get();  // 这里显示的是实际去重后的数量

            serverDetailsText.setText(text);
        });
    }

    private void updateSpinnerList() {
        synchronized (discoveredServers) {
            discoveredServers.clear();

            if (serverInfoList.isEmpty()) {
                discoveredServers.add("无服务器");
            } else {
                discoveredServers.add("请选择服务器 (" + serverInfoList.size() + "个)");

                // 按IP地址排序
                Collections.sort(serverInfoList, new Comparator<ServerInfo>() {
                    @Override
                    public int compare(ServerInfo s1, ServerInfo s2) {
                        try {
                            String[] ip1 = s1.ipAddress.split("\\.");
                            String[] ip2 = s2.ipAddress.split("\\.");

                            for (int i = 0; i < 4; i++) {
                                int num1 = Integer.parseInt(ip1[i]);
                                int num2 = Integer.parseInt(ip2[i]);
                                if (num1 != num2) return Integer.compare(num1, num2);
                            }

                            int port1 = Integer.parseInt(s1.port);
                            int port2 = Integer.parseInt(s2.port);
                            return Integer.compare(port1, port2);

                        } catch (Exception e) {
                            return s1.ipAddress.compareTo(s2.ipAddress);
                        }
                    }
                });

                for (ServerInfo info : serverInfoList) {
                    discoveredServers.add(info.getDisplayName());
                }
            }

            mainHandler.post(() -> {
                spinnerAdapter.notifyDataSetChanged();
                if (!serverInfoList.isEmpty()) {
                    serverSpinner.setSelection(1);
                }
            });
        }
    }

    private void clearServersSilently() {
        synchronized (discoveredServers) {
            discoveredServers.clear();
            serverInfoList.clear();
            serverIds.clear();
            serverCountMap.clear();
            uniqueServers.set(0);

            discoveredServers.add("无服务器");

            mainHandler.post(() -> {
                spinnerAdapter.notifyDataSetChanged();
                serverSpinner.setSelection(0);
                serverDetailsText.setText("请先搜索服务器");
            });
        }
    }

    public void clearServers(View view) {
        clearServersSilently();
        statusText.setText("就绪");
        Toast.makeText(this, "已清除所有服务器", Toast.LENGTH_SHORT).show();
    }

    private void stopDiscovery() {
        if (udpTransceiver != null) {
            udpTransceiver.stop();
        }

        runOnUiThread(() -> {
            stopButton.setEnabled(false);
            progressBar.setVisibility(View.GONE);
            progressBar.setProgress(0);
        });

        isSearching.set(false);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (udpTransceiver != null) {
            udpTransceiver.stop();
        }
    }

    private static class ServerInfo {
        String serverName;
        String hostName;
        String ipAddress;
        String port;
        String clientCount;
        String maxClients;
        String computerId;
        String responseSource;
        String rawResponse;

        String getDisplayName() {
            String shortId = computerId.length() > 8 ? computerId.substring(0, 8) + "..." : computerId;
            return serverName + " (" + ipAddress + ":" + port + ") [" + shortId + "]";
        }

        String getDetails() {
            return "服务器名称: " + serverName + "\n" +
                    "主机名: " + hostName + "\n" +
                    "IP地址: " + ipAddress + "\n" +
                    "端口: " + port + "\n" +
                    "客户端数: " + clientCount + "/" + maxClients + "\n" +
                    "计算机ID: " + computerId + "\n" +
                    "响应来源: " + responseSource + "\n" +
                    "发现时间: " + new java.util.Date().toString() + "\n\n" +
                    "原始响应:\n" + (rawResponse != null ?
                    rawResponse.substring(0, Math.min(rawResponse.length(), 200)) : "无");
        }
    }
}