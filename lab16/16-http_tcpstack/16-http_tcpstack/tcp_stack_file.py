#!/usr/bin/python2

import os
import sys
import string
import socket
import struct
from time import sleep

data = string.digits + string.lowercase + string.uppercase

def server(port, filename):
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    s.bind(('0.0.0.0', int(port)))
    s.listen(3)
    
    cs, addr = s.accept()
    print addr
    
    with open(filename, 'wb') as f:
        while True:
            data = cs.recv(1024)
            if data:
                f.write(data)
            else:
                break
    
    s.close()


def client(ip, port, filename):
    s = socket.socket()
    s.connect((ip, int(port)))

    f = open('client-input.dat', 'r')
    file_str = f.read()
    length = len(file_str)
    i = 0

    while length > 0:
        send_len = min(length, 100000)
        s.send(file_str[i: i+send_len])
        sleep(0.1)
        print("send:{0}, remain:{1}, total: {2}/{3}".format(i, length, i, i+length))
        length -= send_len
        i += send_len
    
    f.close()
    s.close()

if __name__ == '__main__':
    if sys.argv[1] == 'server':
        server(sys.argv[2], "server-output.dat")
    elif sys.argv[1] == 'client':
        client(sys.argv[2], sys.argv[3], "client-input.dat")