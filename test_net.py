#!/usr/bin/env python3

import socket
import time

# Создать Unix domain socket
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)

try:
    # Подключиться к демону
    s.connect("/tmp/network_daemon.sock")
    
    # Отправить команду set_dynamic
    cmd = "set_dynamic eth0"
    print(f"Отправка команды: {cmd}")
    s.send(cmd.encode())
    
    # Ожидать ответа с повторными попытками
    max_attempts = 3
    attempt = 0
    while attempt < max_attempts:
        time.sleep(30)  # Задержка 10 секунд для ожидания ответа от dhcpcd
        try:
            response = s.recv(2048).decode()
            print(f"Ответ от демона: {response}")
            if not response.startswith("Не удалось получить IP"):
                break
            print(f"Попытка {attempt + 1} не удалась, повтор...")
            attempt += 1
            if attempt < max_attempts:
                print(f"Повторная отправка команды: {cmd}")
                s.send(cmd.encode())
        except socket.timeout:
            print(f"Тайм-аут получения ответа, попытка {attempt + 1}/{max_attempts}")
            attempt += 1
            if attempt < max_attempts:
                print(f"Повторная отправка команды: {cmd}")
                s.send(cmd.encode())
        except Exception as e:
            print(f"Ошибка при выполнении команды '{cmd}': {e}")
            break
    
except Exception as e:
    print(f"Ошибка подключения к демону: {e}")
    
finally:
    # Закрыть сокет
    s.close()