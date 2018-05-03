# Coursework: Introduction to Computer Networks

Three projects are included in this folder.

## Ping-Pong and World Wide Web

A client and a server are implemented and can communicate with each other. The server can both run in "ping-pong" mode, and in "www" mode (only support HTTP GET requests).

## Reliable File Transfer Protocol

A TCP-like (but much simplified) protocol built on `SOCK_DGRAM` sockets. This protocol uses cyclic redundancy check and sliding window algorithm, which support reliable transmission over unreliable network, even under extreme circumstances when 95% packets are lost, duplicated, delay, reorder, or mangle.

## Intra-Domain Routing Protocols 

Both link-state (LS) and distance-vector (DV) protocols are implemented. Besides, several simple test cases and some extreme test cases (giant graph and very complicated scenario) are included in the folder for your reference.
