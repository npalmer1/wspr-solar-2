# Test FTP server for ftp_test: python3 ftp_server.py DIR MODE  (MODE: normal | noepsv)
# Prints the port it listens on, then serves DIR read-only to anonymous until killed. Needs pyftpdlib.
import sys
from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler
from pyftpdlib.servers import FTPServer

root, mode = sys.argv[1], sys.argv[2]
auth = DummyAuthorizer()
auth.add_anonymous(root)

class Handler(FTPHandler):
    authorizer = auth
    # over 75 characters, so pyftpdlib sends it as a multi-line reply: "220-...", a line with no code, "220 "
    banner = ("Test FTP server for WSPR Solar 2 - a long greeting, as NOAA's server sends one\r\n"
              "  a middle line of the greeting, with no reply code")

    def ftp_EPSV(self, line):
        if mode == "noepsv":
            self.respond("502 EPSV not implemented.")
            return
        return super().ftp_EPSV(line)

Handler.proto_cmds = dict(FTPHandler.proto_cmds)
server = FTPServer(("127.0.0.1", 0), Handler)
print(server.socket.getsockname()[1], flush=True)
server.serve_forever()
