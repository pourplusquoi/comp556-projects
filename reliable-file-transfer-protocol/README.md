# COMP 556 Project 2

## How to Make
Go to the directory where ``Makefile`` locates, use command ``make`` to compile ``sendfile.cpp`` into ``sendfile`` and ``recvfile.cpp`` into ``recvfile``.

## How to Test

**\<IMPORTANT\> we designed our program such that subdir should begin with `./`**

Go to the directory where the executable file locates, use command ``./sendfile -r <recv host>:<recv port> -f <subdir>/<filename>`` to send, and use command ``./recvfile -p <recv port>`` to receive.

Since we have implemented a timer for ``sendfile``, it would be better to run ``recvfile`` first. Otherwise, the measured time would be inaccurate (it would include waiting time).

## Packet Format

In ``packheader.h``, we implemented a function to calculate 32-bit Cyclic Redundancy Check (CRC) value by table look-up. We also defined a struct to store the header of each packet, called ``PackHeader``, which includes ``crc32_val``: CRC value of packet, ``serial_id``: the serial number of packet, ``packet_size``: the size of packet.

Our packet size is 10024. The first 12 bytes of each packet is its header, the following 10000 bytes are the content of message.

## Protocols and Algorithms

We used sliding window algorithm in our project. The window size is fixed to be 100. 

### Sender

Sender is only allowed to send the packets whose serial number is within the window, and cache them in case of retransmission. When an acknowledgement without corruption (using CRC)  from receiver is received, the window slides until it meets the unacknowledged packet. The packets outside of the window are removed from the cache. If an acknowledgement is corrupted, just ignore it.

#### Retransmission

If the resend pool is not empty, sender would resend the packet with the smallest serial number in the resend pool, update the time stamp in cache, and remove it from the resend pool.

#### Timeout Detection

When a packet is inserted into cache, it is attached with a time stamp. Timeout detection is performed in every loop: for every packet in cache, calculate the time elapsed using attached time stamp; if a packet is timeout, insert its serial number into resend pool.

#### CRC

We choose to use crc-32 algorithm in which the $C(x)$ is defined as follows:

$C(x)=x^{32}+x^{26}+x^{23}+x^{22}+x^{16}+x^{12}+x^{11}+x^{10}+x^{8}+x^{7}+x^{5}+x^{4}+x^{2}+x^{1}+x^{0}$

The implementation of our crc-32 algorithm uses a pre-calculated table, `crc32_table` to get the remainder of any given one-byte number (or we can say 5-byte if the added 32 0's are counted).

For example, given a one-byte number $1$, then the dividend is `0x100000000` (32 `0`'s added), and the divisor, as mentioned above $C(x)$, is `0x104c11db7`. We can do the modulo-2 division ourselves and get the remainder as `0x04c11db7`; on the other hand, in our code, you can see the value of `crc32_table[1]` is exactly `0x04c11db7`. 

With the help of this table, now we can see why we have the following formula to calculate the crc.

```
crc = (crc << 8) ^ crc32_table[((crc >> 24) ^ *buf++) & 255];
```

The variable `crc` represents current remainder value after considering all the bytes before `buf`. 

`crc >> 24` extracts the most significant byte of the current remainder value.

`(crc >> 24) ^ *buf` adds the current byte to the most significant byte and gets the current dividend value.

`crc32_table[(crc >> 24) ^ *buf]` helps us calculate the new remainder.

Because `crc >> 24` means we discards the less significant 24 bits of previous remainder, we need to add these 24 bits back to the new remainder. Also, we need to adjust the bit position of these 24 bits by shifting left 8 bits, because the calculation moves forward one byte per iteration.

#### Pseudocode

```python
while True:
  if insideWindow and resend.empty():
  	packet = nextPacket()
    cache[packet.serial].time = currentTime()
    cache[packet.serial].pack = nextPacket()
   	send(packet)
  elif not resend.empty():
    serial = resend.begin()
    resend.erase(serial)
    cache[serial].time = currentTime()
    send(cache[serial].pack)
  ack = recv()
  for cell in cache:
    if cell.pack.serial <= ack:
      cache.erase(cell.pack.serial)
  for cell in cache:
    if timeout(cell):
      resend.insert(cell.pack.serial)
```

### Receiver

When a packet is received from sender, check whether the message is corrupted (using CRC) or its serial number is outside the window. If yes, ignore it; otherwise, check whether the packet is in-order. If yes, directly write it together with in-order cached message into file; otherwise, cache it for future write.

#### Acknoledgement

When a packet without corruption is received, receiver always acknowledge the serial number where all the packets, whose serial numbers are smaller this this one, are successdully received. If a corrupted packet is received, them receiver does nothing but ignore it.

#### Pseudocode

```python
while True:
  packet = recv()
  if isCorrupted(packet):
    continue
  if insideWindow(packet):
    if inplace(cache, packet):
      writeInplace(cache, packet, file)
    else:
      cache[packet.serial] = packet
  send(highestRecievedSerial()) # before which all packets are received
```

### Hyperparameter Tuning

#### Timeout Selection

We first measure Round-Trip-Time (RTT) by ``ping water.clear.rice.edu`` on ``cai.cs.rice.edu`` and got approximately 0.6 msec. Therefore, we set the timeout to be 1 msec.

#### Window Size Selection

The window size should be large enough to fully utilize the available bandwidth, but it should be kept within the capacity of sender, receiver, and network. Therefore, we set the window size to be 100.

## New Features

We consider the case where the first packet inside the window is lost, however, all the remaining packets inside the window are accepted by receiver. Under this circumstance, recveiver can only send acknowledgement for the last packet before window. Thus, all the packets inside window would be timeout. However, what acrually needs retransmission is only the first packet.

Therfore, we resend the first packet in resend pool, and go back to send new packets. After several rounds, we perform next resend task. Ideally, the resend packet is accepted, and the window can slide right for entire window size. The "unnecessary" resend tasks are thus skipped.

This trick makes our program run faster, especially at time when the network is relatively reliable.

## Memory Usage Analysis

### Sender

Our packet size is fixed to be 10024 bytes, and our window size is fixed to be 100. Every cached packet is attached with a time stamp of 16 bytes. In the worst case, every packet inside the window is cached, and the serial number of every packet (4 bytes) is in resend pool. In this case, the required memory is approximately 1.004 MB.

### Receiver

In the worse case, every accepted packet is out-of-order. In this case, it is needed to cache the serial number and the content of every packet. This requires approximately 1.001 MB memory.