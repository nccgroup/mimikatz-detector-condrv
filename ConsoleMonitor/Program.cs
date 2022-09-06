using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
using System.Threading;
using System.Text;

namespace ConsoleMonitor
{
    static class ConsoleMonitor
    {
        public const int
            INVALID_HANDLE_VALUE = (-1),
            NULL = 0,
            ERROR_SUCCESS        = 0,
            FILE_READ_DATA        = (0x0001),      
            FILE_SHARE_READ        = 0x00000001,
            OPEN_EXISTING        = 3,
            GENERIC_READ              = unchecked((int)0x80000000),
            METHOD_BUFFERED         = 0,
            METHOD_NEITHER          = 3,
            FILE_ANY_ACCESS         = 0,
            FILE_DEVICE_VIRTUAL_DISK = 0x00000024,
            GENERIC_WRITE            = 0x40000000;

        [DllImport("Kernel32.dll", ExactSpelling = true, CharSet = CharSet.Auto, SetLastError = true)]
        public static extern bool CloseHandle(int hHandle);

        [DllImport("Kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        public static extern int CreateFile(String lpFileName, int dwDesiredAccess, int dwShareMode, IntPtr lpSecurityAttributes,
            int dwCreationDisposition, int dwFlagsAndAttributes,
            int hTemplateFile
        );
        
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool ReadFile(int hDevice, [Out] byte[] lpBuffer, uint nNumberOfBytesToRead, ref uint lpNumberOfBytesRead, IntPtr lpOverlapped);

        [DllImport("Kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        static public extern bool DeviceIoControl(int hDevice, int dwIoControlCode, byte[] InBuffer, int nInBufferSize, IntPtr OutBuffer, int nOutBufferSize, ref int pBytesReturned, int pOverlapped);

        static public int hDriver = INVALID_HANDLE_VALUE;

        public const int IOCTL_READCONLOG = 0x500038;
        public static bool run = true;

        [STAThread]
        static void Main()
        {
            Application.SetHighDpiMode(HighDpiMode.SystemAware);
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new Form1());
        }

        public class StreamString
        {
            private Stream ioStream;
            private UnicodeEncoding streamEncoding;

            public StreamString(Stream ioStream)
            {
                this.ioStream = ioStream;
                streamEncoding = new UnicodeEncoding();
            }

            public string ReadString()
            {
                int len = 1024;

                byte[] inBuffer = new byte[len];
                if (ioStream.Read(inBuffer, 0, len) == 0)
                {
                    run = false;
                }

                return streamEncoding.GetString(inBuffer);
            }

        }


        public static void ServerThread(TextBox tb)
        {
            NamedPipeServerStream pipeServer = new NamedPipeServerStream("ConMonDrvPipe", PipeDirection.InOut, 1);

            pipeServer.WaitForConnection();
            
            try
            {
                StreamString ss = new StreamString(pipeServer);
                while (run)
                {
                    string s = ss.ReadString();
                    if (Encoding.ASCII.GetBytes(s)[0] == 0x08)
                    {
                        tb.Text = tb.Text.Substring(0, tb.Text.Length - 1);
                    }
                    else if ((Encoding.ASCII.GetBytes(s)[0] == 0x0a))
                    {
                        tb.AppendText("\r\n");
                    }
                    /*else if ((Encoding.ASCII.GetBytes(s)[0] == 0x0d))
                    {
                        tb.AppendText("\r\n");
                    }*/
                    else
                    {
                        tb.AppendText(s.Replace("\n\r", "\r\n")); // mimikatz demo fix
                    }
                    tb.SelectionStart = tb.Text.Length;
                    tb.ScrollToCaret();
                    tb.Refresh();
                }
            }
            catch (IOException e)
            {
                MessageBox.Show(String.Format("Done: {0}", e.Message), "Error");
            }
            pipeServer.Close();
        }

        static public bool Start(TextBox tb)
        {
            Task.Run(() => {
                ServerThread(tb);
            });
            

            return true;
         }

    }
}
