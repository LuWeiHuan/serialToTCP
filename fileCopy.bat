@echo off
chcp 65001 >nul

setlocal

set IP=192.168.1.25
set PORT=19000
set MESSAGE=discover_com2tcp_server
set MESSAGE=ctrlInfo:exit
set TIMEOUT_MS=500
 
echo 正在向 %IP%:%PORT% 发送UDP数据: %MESSAGE%

powershell -Command "$ip='%IP%'; $port=%PORT%; $message='%MESSAGE%'; $timeout=%TIMEOUT_MS%; $udpClient=New-Object System.Net.Sockets.UdpClient; $udpClient.Client.ReceiveTimeout=$timeout; try { $udpClient.Connect($ip,$port); $data=[System.Text.Encoding]::ASCII.GetBytes($message); $bytesSent=$udpClient.Send($data,$data.Length); Write-Host '成功发送' $bytesSent '字节数据'; Write-Host '等待回复...（超时时间：'$timeout'ms）'; $remoteEndpoint=New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any,0); $receivedData=$udpClient.Receive([ref]$remoteEndpoint); $receivedMessage=[System.Text.Encoding]::ASCII.GetString($receivedData); Write-Host '收到回复来自' $remoteEndpoint.Address ':' $remoteEndpoint.Port; Write-Host '回复内容:' $receivedMessage } catch [System.Net.Sockets.SocketException] { if ($_.Exception.SocketErrorCode -eq 'TimedOut') { Write-Host '等待超时，未收到回复' -ForegroundColor Yellow } else { Write-Host '错误:' $_.Exception.Message -ForegroundColor Red } } catch { Write-Host '错误:' $_.Exception.Message -ForegroundColor Red } finally { $udpClient.Close() }"

echo 操作完成
endlocal

copy /Y .\com2tcp_server.exe "\\Hx-qxb-fs\器械部共享盘\软件包\远程工具\远程串口"