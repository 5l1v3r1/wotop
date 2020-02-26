Web-On-Top-Of-Protocol (WOTOP)
========================

```
██╗    ██╗ ██████╗ ████████╗ ██████╗ ██████╗
██║    ██║██╔═══██╗╚══██╔══╝██╔═══██╗██╔══██╗
██║ █╗ ██║██║   ██║   ██║   ██║   ██║██████╔╝
██║███╗██║██║   ██║   ██║   ██║   ██║██╔═══╝
╚███╔███╔╝╚██████╔╝   ██║   ╚██████╔╝██║
 ╚══╝╚══╝  ╚═════╝    ╚═╝    ╚═════╝ ╚═╝  
```

# Introduction
**WOTOP** is a tool meant to tunnel any sort of traffic over a standard HTTP channel.

Useful for scenarios where there's a proxy filtering all traffic except standard HTTP(S) traffic. Unlike other tools which either require you to be behind a proxy which let's you pass arbitrary traffic (possibly after an initial CONNECT request), or tools which work only for SSH, this imposes no such restrictions.

# Working
Assuming you want to use SSH to connect to a remote machine where you don't have root privileges.

There will be 7 entities:

1. Client (Your computer, behind the proxy)
2. Proxy (Evil)
3. Target Server (The remote machine you want to SSH to, from Client)
4. Client WOTOP process
5. Target WOTOP process
6. Client SSH process
7. Target SSH process

If there was no proxy, the communication would be something like:
```
Client -> Client SSH process -> Target Server -> Target SSH process
```

In this scenario, here's the proposed method:
```
Client -> Client SSH process -> Client WOTOP process -> Proxy -> Target WOTOP process -> Target SSH process -> Target Server
```

**WOTOP** simply wraps all the data in HTTP packets, and buffers them accordingly.

Another even more complicated scenario would be if you have an external utility server, and need to access another server's resources from behind a proxy. In this case, *wotop* will still run on your external server, but instead of using `localhost` in the second command (Usage section), use the hostname of the target machine which has the host.

# Usage
On the client machine:
```
./wotop <client-hop-port> <server-host-name> <server-hop-port>
```

On the target machine:
```
./wotop <server-hop-port> localhost <target-port> SERVER
```
(Note the keyword SERVER at the end)

In case of SSH, the target-port would be 22.
Now once these 2 are running, to SSH you would run the following:

```
ssh <target-machine-username>@localhost -p <client-hop-port>
```

*Note*: The keyword server tells *wotop* which side of the connection has to be over HTTP.

# Contributing
Pull Requests are more than welcome! :smile:

# Planned features
* Better and adaptive buffering
* Better CLI flags interface
* Optional encrypting of data
* Parsing of .ssh/config file for hosts
* Web interface for remote server admin
* Web interface for local host
* Daemon mode for certain configs

# Bugs
* Currently uses a 100ms sleep after every send/receive cycle to bypass some memory error (not yet eliminated).
* HTTP Responses may come before HTTP Requests. Let me know if you know of some proxy which blocks such responses.
* Logger seems to be non-thread-safe, despite locking. Leads to memory errors, and thus disabled for now.

