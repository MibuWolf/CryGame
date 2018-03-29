using System.IO;
using System;
using System.Text;
using System.Security.Cryptography;

namespace CEVResourceBridge.Utils
{
    public enum Endian
    {
        BIG_ENDIAN,
        LITTLE_ENDIAN
    }

    public class ByteArray
    {
        private System.IO.MemoryStream _memoryStream;
        private BinaryReader _reader;
        private BinaryWriter _writer;
        private Endian _endian;

        public ByteArray()
        {
            this._memoryStream = new System.IO.MemoryStream();
            this._init();
        }

        public ByteArray(System.IO.MemoryStream __ms)
        {
            this._memoryStream = __ms;
            this._init();
        }

        public ByteArray(byte[] buffer, int __startPosition = 0, int __length = -1)
        {
            if (__length == -1)
            {
                __length = buffer.Length;
            }
            this._memoryStream = new System.IO.MemoryStream();
            _memoryStream.Write(buffer, 0, buffer.Length);
            _memoryStream.Position = 0;
            this._init();
        }

        private void _init()
        {
            _reader = new BinaryReader(_memoryStream);
            _writer = new BinaryWriter(_memoryStream);

            _endian = Endian.LITTLE_ENDIAN;
        }

        public Endian endian
        {
            set
            {
                this._endian = value;
            }
            get
            {
                return this._endian;
            }
        }

        public uint length
        {
            get
            {
                return (uint)this._memoryStream.Length;
            }
            set
            {
                this._memoryStream.SetLength(value);
            }
        }

        public uint position
        {
            get { return (uint)_memoryStream.Position; }
            set { _memoryStream.Position = value; }
        }

        public uint bytesAvailable
        {
            get { return (uint)(this._memoryStream.Length - this._memoryStream.Position); }
        }

        public byte[] GetBuffer()
        {
            return _memoryStream.GetBuffer();
        }

        public byte[] ToArray()
        {
            return _memoryStream.ToArray();
        }

        internal byte[] _readStreamBytesEndian(int __length)
        {
            byte[] __result = this._reader.ReadBytes(__length);
            if (this.endian == Endian.BIG_ENDIAN)
            {
                Array.Reverse(__result);
            }
            return __result;
        }

        internal void _writeStreamBytesEndian(byte[] __bytes)
        {
            if (__bytes == null)
            {
                return;
            }
            if (this.endian == Endian.BIG_ENDIAN)
            {
                Array.Reverse(__bytes);
            }
            this._memoryStream.Write(__bytes, 0, __bytes.Length);
        }

        public bool readBoolean()
        {
            return this._memoryStream.ReadByte() == 1;
        }

        public void writeBoolean(bool __boolean)
        {
            this._memoryStream.WriteByte(__boolean ? ((byte)1) : ((byte)0));
        }

        public bool[] readBooleanArray()
        {
            int __length = this.readU29Int();
            bool[] __result = new bool[__length];
            for (int i = 0; i < __length; i++)
            {
                __result[i] = this.readBoolean();
            }
            return __result;
        }

        public void writeBooleanArray(bool[] __value)
        {
            int __length = __value.Length;
            this.writeU29Int(__length);
            for (int i = 0; i < __length; i++)
            {
                this.writeBoolean(__value[i]);
            }
        }

        public byte readUnsignedByte()
        {
            return (byte)this._memoryStream.ReadByte();
        }

        public void writeUnsignedByte(byte __ubyte)
        {
            this._memoryStream.WriteByte(__ubyte);
        }

        public byte[] readUnsignedByteArray()
        {
            int __length = this.readU29Int();
            byte[] __result = new byte[__length];
            for (int i = 0; i < __length; i++)
            {
                __result[i] = this.readUnsignedByte();
            }
            return __result;
        }

        public void writeUnsignedByteArray(byte[] __value)
        {
            int __length = __value.Length;
            this.writeU29Int(__length);
            for (int i = 0; i < __length; i++)
            {
                this.writeUnsignedByte(__value[i]);
            }
        }

        public sbyte readSignedByte()
        {
            return (sbyte)this._memoryStream.ReadByte();
        }

        public void writeSignedByte(sbyte __byte)
        {
            this._memoryStream.WriteByte((byte)__byte);
        }

        public sbyte[] readSignedByteArray()
        {
            int __length = this.readU29Int();
            sbyte[] __result = new sbyte[__length];
            for (int i = 0; i < __length; i++)
            {
                __result[i] = this.readSignedByte();
            }
            return __result;
        }

        public void writeSignedByteArray(sbyte[] __value)
        {
            int __length = __value.Length;
            this.writeU29Int(__length);
            for (int i = 0; i < __length; i++)
            {
                this.writeSignedByte(__value[i]);
            }
        }

        public ushort readUnsignedShort()
        {
            byte[] bytes = this._readStreamBytesEndian(sizeof(ushort));
            return BitConverter.ToUInt16(bytes, 0);
        }

        public void writeUnsignedShort(ushort __ushort)
        {
            byte[] bytes = BitConverter.GetBytes((ushort)__ushort);
            _writeStreamBytesEndian(bytes);
        }

        public ushort[] readUnsignedShortArray()
        {
            int __length = this.readU29Int();
            ushort[] __result = new ushort[__length];
            for (int i = 0; i < __length; i++)
            {
                __result[i] = this.readUnsignedShort();
            }
            return __result;
        }

        public void writeUnsignedShortArray(ushort[] __value)
        {
            int __length = __value.Length;
            this.writeU29Int(__length);
            for (int i = 0; i < __length; i++)
            {
                this.writeUnsignedShort(__value[i]);
            }
        }

        public short readShort()
        {
            byte[] bytes = this._readStreamBytesEndian(sizeof(short));
            return BitConverter.ToInt16(bytes, 0);
        }

        public void writeShort(short __short)
        {
            byte[] bytes = BitConverter.GetBytes((short)__short);
            _writeStreamBytesEndian(bytes);
        }

        public short[] readShortArray()
        {
            int __length = this.readU29Int();
            short[] __result = new short[__length];
            for (int i = 0; i < __length; i++)
            {
                __result[i] = this.readShort();
            }
            return __result;
        }

        public void writeShortArray(short[] __value)
        {
            int __length = __value.Length;
            this.writeU29Int(__length);
            for (int i = 0; i < __length; i++)
            {
                this.writeShort(__value[i]);
            }
        }

        public uint readUnsignedInt()
        {
            byte[] bytes = this._readStreamBytesEndian(sizeof(uint));
            return BitConverter.ToUInt32(bytes, 0);
        }

        public void writeUnsignedInt(uint __uint)
        {
            byte[] bytes = new byte[4];

            bytes[3] = (byte)(0xFF & (__uint >> 24));
            bytes[2] = (byte)(0xFF & (__uint >> 16));
            bytes[1] = (byte)(0xFF & (__uint >> 8));
            bytes[0] = (byte)(0xFF & (__uint >> 0));

            this._writeStreamBytesEndian(bytes);
        }

        public uint[] readUnsignedIntArray()
        {
            int __length = this.readU29Int();
            uint[] __result = new uint[__length];
            for (int i = 0; i < __length; i++)
            {
                __result[i] = this.readUnsignedInt();
            }
            return __result;
        }

        public void writeUnsignedIntArray(uint[] __value)
        {
            int __length = __value.Length;
            this.writeU29Int(__length);
            for (int i = 0; i < __length; i++)
            {
                this.writeUnsignedInt(__value[i]);
            }
        }

        public int readInt()
        {
            byte[] bytes = this._readStreamBytesEndian(sizeof(int));
            return BitConverter.ToInt32(bytes, 0);
        }

        public void writeInt(int __int)
        {
            byte[] bytes = BitConverter.GetBytes(__int);
            _writeStreamBytesEndian(bytes);
        }

        public int[] readIntArray()
        {
            int __length = this.readU29Int();
            int[] __result = new int[__length];
            for (int i = 0; i < __length; i++)
            {
                __result[i] = this.readInt();
            }
            return __result;
        }

        public void writeIntArray(int[] __value)
        {
            int __length = __value.Length;
            this.writeU29Int(__length);
            for (int i = 0; i < __length; i++)
            {
                this.writeInt(__value[i]);
            }
        }

        public int readU29Int()
        {
            int __acc = this._reader.ReadByte();
            int __tmp;
            if (__acc < 128)
            {
                return __acc;
            }
            else
            {
                __acc = (__acc & 0x7f) << 7;
                __tmp = this._reader.ReadByte();
                if (__tmp < 128)
                {
                    __acc = __acc | __tmp;
                }
                else
                {
                    __acc = (__acc | __tmp & 0x7f) << 7;
                    __tmp = this._reader.ReadByte();
                    if (__tmp < 128)
                    {
                        __acc = __acc | __tmp;
                    }
                    else
                    {
                        __acc = (__acc | __tmp & 0x7f) << 8;
                        __tmp = this._reader.ReadByte();
                        __acc = __acc | __tmp;
                    }
                }
            }
            //To sign extend a value from some number of bits to a greater number of bits just copy the sign bit into all the additional bits in the new format.
            //convert/sign extend the 29bit two's complement number to 32 bit
            int __mask = 1 << 28; // mask
            int __r = -(__acc & __mask) | __acc;
            return __r;

            //The following variation is not portable, but on architectures that employ an
            //arithmetic right-shift, maintaining the sign, it should be fast.
            //s = 32 - 29;
            //r = (x << s) >> s;
        }

        public void writeU29Int(int __value)
        {
            //Sign contraction - the high order bit of the resulting value must match every bit removed from the number
            //Clear 3 bits
            __value &= 0x1fffffff;
            if (__value < 0x80)
            {
                this._memoryStream.WriteByte((byte)__value);
            }
            else
            {
                if (__value < 0x4000)
                {
                    this._memoryStream.WriteByte((byte)(__value >> 7 & 0x7f | 0x80));
                    this._memoryStream.WriteByte((byte)(__value & 0x7f));
                }
                else
                {
                    if (__value < 0x200000)
                    {
                        this._memoryStream.WriteByte((byte)(__value >> 14 & 0x7f | 0x80));
                        this._memoryStream.WriteByte((byte)(__value >> 7 & 0x7f | 0x80));
                        this._memoryStream.WriteByte((byte)(__value & 0x7f));
                    }
                    else
                    {
                        this._memoryStream.WriteByte((byte)(__value >> 22 & 0x7f | 0x80));
                        this._memoryStream.WriteByte((byte)(__value >> 15 & 0x7f | 0x80));
                        this._memoryStream.WriteByte((byte)(__value >> 8 & 0x7f | 0x80));
                        this._memoryStream.WriteByte((byte)(__value & 0xff));
                    }
                }
            }
        }

        public int[] readU29IntArray()
        {
            int __length = this.readU29Int();
            int[] __result = new int[__length];
            for (int i = 0; i < __length; i++)
            {
                __result[i] = this.readU29Int();
            }
            return __result;
        }

        public void writeU29IntArray(int[] __value)
        {
            int __length = __value.Length;
            this.writeU29Int(__length);
            for (int i = 0; i < __length; i++)
            {
                this.writeU29Int(__value[i]);
            }
        }

        public float readFloat()
        {
            return BitConverter.ToSingle(this._readStreamBytesEndian(sizeof(float)), 0);
        }

        public void writeFloat(float __float)
        {
            byte[] __bytes = BitConverter.GetBytes(__float);
            this._writeStreamBytesEndian(__bytes);
        }

        public float[] readFloatArray()
        {
            int __length = this.readU29Int();
            float[] __result = new float[__length];
            for (int i = 0; i < __length; i++)
            {
                __result[i] = this.readFloat();
            }
            return __result;
        }

        public void writeFloatArray(float[] __value)
        {
            int __length = __value.Length;
            this.writeU29Int(__length);
            for (int i = 0; i < __length; i++)
            {
                this.writeFloat(__value[i]);
            }
        }

        public double readDouble()
        {
            return BitConverter.ToDouble(_readStreamBytesEndian(sizeof(double)), 0);
        }

        public void writeDouble(double __double)
        {
            byte[] __bytes = BitConverter.GetBytes(__double);
            this._writeStreamBytesEndian(__bytes);
        }

        public double[] readDoubleArray()
        {
            int __length = this.readU29Int();
            double[] __result = new double[__length];
            for (int i = 0; i < __length; i++)
            {
                __result[i] = this.readDouble();
            }
            return __result;
        }

        public void writeDoubleArray(double[] __value)
        {
            int __length = __value.Length;
            this.writeU29Int(__length);
            for (int i = 0; i < __length; i++)
            {
                this.writeDouble(__value[i]);
            }
        }

        public void readBytes(ByteArray __bytes, uint __offset, uint __length)
        {
            byte[] readContent = new byte[__length];
            this._memoryStream.Read(readContent, (int)__offset, (int)__length);
            __bytes.writeBytes(new ByteArray(readContent), 0, (uint)readContent.Length);
        }

        public void writeBytes(ByteArray __bytes, uint offset, uint length)
        {
            this._memoryStream.Write(__bytes.ToArray(), (int)offset, (int)length);
        }

        public ByteArray[] readBytesArray()
        {
            int __length = this.readU29Int();
            ByteArray[] __result = new ByteArray[__length];
            for (int i = 0; i < __length; i++)
            {
                uint __streamLength = (uint)this.readU29Int();
                ByteArray __newBytes = new ByteArray();
                this.readBytes(__newBytes, 0, __streamLength);
                __result[i] = __newBytes;
            }
            return __result;
        }

        public void writeBytesArray(ByteArray[] __value)
        {
            int __length = __value.Length;
            this.writeU29Int(__length);
            for (int i = 0; i < __length; i++)
            {
                ByteArray __stream = __value[i];
                uint __streamLength = __stream.length;
                this.writeU29Int((int)__streamLength);
                this.writeBytes(__stream, 0, __streamLength);
            }
        }

        public string readUTFBytes(int __length)
        {
            if (__length == 0)
            {
                return string.Empty;
            }
            UTF8Encoding utf8 = new UTF8Encoding(false, true);
            byte[] encodedBytes = this._reader.ReadBytes(__length);
            string decodedString = utf8.GetString(encodedBytes, 0, encodedBytes.Length);
            return decodedString;
        }

        public void writeUTFBytes(string __utf)
        {
            //Length - max 65536.
            UTF8Encoding utf8Encoding = new UTF8Encoding();
            byte[] buffer = utf8Encoding.GetBytes(__utf);
            if (buffer.Length > 0)
            {
                this._writer.Write(buffer);
            }
        }

        public string readUTF()
        {
            int __length = this.readU29Int();
            return this.readUTFBytes(__length);
        }

        public void writeUTF(string __utf)
        {
            //null string is not accepted
            //in case of custom serialization leads to TypeError: Error #2007: Parameter value must be non-null.  at flash.utils::ObjectOutput/writeUTF()

            //Length - max 65536.
            UTF8Encoding utf8Encoding = new UTF8Encoding();
            int byteCount = utf8Encoding.GetByteCount(__utf);
            byte[] buffer = utf8Encoding.GetBytes(__utf);
            this.writeU29Int((short)byteCount);
            if (buffer.Length > 0)
            {
                this._writer.Write(buffer);
            }
        }

        public string[] readUTFArray()
        {
            int __length = this.readU29Int();
            string[] __result = new string[__length];
            for (int i = 0; i < __length; i++)
            {
                __result[i] = this.readUTF();
            }
            return __result;
        }

        public void writeUTFArray(string[] __value)
        {
            int __length = __value.Length;
            this.writeU29Int(__length);
            for (int i = 0; i < __length; i++)
            {
                this.writeUTF(__value[i]);
            }
        }

        public string ComputeMD5()
        {
            MD5 md5 = new MD5CryptoServiceProvider();
            byte[] retVal = md5.ComputeHash(this.GetBuffer());
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < retVal.Length; i++)
            {
                sb.Append(retVal[i].ToString("x2"));
            }
            return sb.ToString();
        }
    }
}
