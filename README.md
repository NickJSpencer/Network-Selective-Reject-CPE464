# Selective Reject (using sliding window)

__RR Packet Header__
* packet sequence number
* checksum
* flag
* RR sequence number

__SREJ Packet Header__
* packet sequence number
* checksum
* flag
* SREJ sequence number

__Flags__
1. Client to server: setup packet
2. Server to client: setup response
3. Server to client: data packet
4. N/A
5. Client to server: RR packet
6. Client to server: SREJ packet
7. Client to server: remote filename packet
8. Server to client: bad filename
9. Client to server: end connection
10. Server to client: final data packet
