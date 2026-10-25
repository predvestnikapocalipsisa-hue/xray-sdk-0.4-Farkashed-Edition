using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;

// Compiled by the embedded PowerShell bootstrap, using Windows .NET Framework.
// No downloaded code is executed. Only the resource directories below are installed.
public sealed class SDKResourceWorker
{
    public const string FileId = "137YxJG65oJrJb1kwUPaxoyCpn9XDn-2n";
    const long KnownArchiveSize = 11450714965L;
    const long KnownResourceSize = 2998494318L;
    const long Reserve = 64L * 1024 * 1024;
    static readonly string[] ResourceFolders = { "rawdata", "groups", "maps", "settings", "gamedata" };
    static readonly Encoding Utf8 = new UTF8Encoding(false, true);
    string target, work, stage, phase = "checking", message = "";
    int parentId;
    long parentStart, downloaded, total = KnownArchiveSize, extracted, extractTotal;
    double rate;
    readonly Stopwatch clock = Stopwatch.StartNew();
    double lastReport;

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool MoveFileEx(string source, string destination, int flags);

    sealed class Paused : Exception { }
    sealed class Remote
    {
        public string Url, Validator;
        public long Size;
    }
    public sealed class ResourceEntry
    {
        public ZipArchiveEntry Entry;
        public string Path;
        public uint Crc;
    }

    static bool IsDirectory(string path) { return Directory.Exists(path); }
    static string Root(string path) { return System.IO.Path.GetFullPath(path).TrimEnd('\\', '/'); }

    public static string SafeName(string name)
    {
        name = name.Replace('\\', '/');
        if (name.Length == 0 || name[0] == '/' || name.IndexOf(':') >= 0 || name.IndexOf('\0') >= 0)
            throw new InvalidDataException("Unsafe resource archive path.");
        string trimmed = name.TrimEnd('/');
        foreach (string part in trimmed.Split('/'))
        {
            if (part.Length == 0 || part == "." || part == ".." || part.EndsWith(".") || part.EndsWith(" ") ||
                part.IndexOfAny(System.IO.Path.GetInvalidFileNameChars()) >= 0 ||
                Regex.IsMatch(part, @"^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\.|$)", RegexOptions.IgnoreCase))
                throw new InvalidDataException("Unsafe resource archive path.");
        }
        return name;
    }

    static bool Selected(string relative)
    {
        string root = relative.Split('/')[0];
        foreach (string folder in ResourceFolders)
            if (String.Equals(folder, root, StringComparison.OrdinalIgnoreCase)) return true;
        return false;
    }

    // ZipArchive does not expose CRC-32 on .NET Framework. Read the central directory
    // separately, including ZIP64 offsets, so every extracted file is checked.
    public static List<uint> ReadCrcs(Stream stream)
    {
        var reader = new BinaryReader(stream, Encoding.UTF8, true);
        int count = (int)Math.Min(stream.Length, 65557);
        stream.Position = stream.Length - count;
        byte[] tail = reader.ReadBytes(count);
        int end = -1;
        for (int i = tail.Length - 22; i >= 0; --i)
            if (BitConverter.ToUInt32(tail, i) == 0x06054b50 && i + 22 + BitConverter.ToUInt16(tail, i + 20) == tail.Length)
            { end = i; break; }
        if (end < 0) throw new InvalidDataException("ZIP directory is missing.");
        if (BitConverter.ToUInt16(tail, end + 4) != 0 || BitConverter.ToUInt16(tail, end + 6) != 0)
            throw new InvalidDataException("Multi-volume ZIP is unsupported.");
        ulong entries = BitConverter.ToUInt16(tail, end + 10);
        ulong offset = BitConverter.ToUInt32(tail, end + 16);
        if (entries == 65535 || offset == UInt32.MaxValue || BitConverter.ToUInt32(tail, end + 12) == UInt32.MaxValue)
        {
            if (end < 20 || BitConverter.ToUInt32(tail, end - 20) != 0x07064b50 ||
                BitConverter.ToUInt32(tail, end - 16) != 0 || BitConverter.ToUInt32(tail, end - 4) != 1)
                throw new InvalidDataException("Invalid ZIP64 locator.");
            ulong location = BitConverter.ToUInt64(tail, end - 12);
            if (location > (ulong)Math.Max(0, stream.Length - 56)) throw new InvalidDataException("Invalid ZIP64 offset.");
            stream.Position = (long)location;
            if (reader.ReadUInt32() != 0x06064b50) throw new InvalidDataException("Invalid ZIP64 directory.");
            stream.Position += 12;
            if (reader.ReadUInt32() != 0 || reader.ReadUInt32() != 0) throw new InvalidDataException("Multi-volume ZIP64 is unsupported.");
            reader.ReadUInt64();
            entries = reader.ReadUInt64();
            reader.ReadUInt64();
            offset = reader.ReadUInt64();
        }
        if (entries > 1000000 || offset > (ulong)stream.Length) throw new InvalidDataException("Invalid ZIP directory size.");
        stream.Position = (long)offset;
        var crcs = new List<uint>((int)entries);
        for (ulong i = 0; i < entries; ++i)
        {
            if (reader.ReadUInt32() != 0x02014b50) throw new InvalidDataException("Invalid ZIP entry.");
            stream.Position += 4;
            ushort flags = reader.ReadUInt16();
            ushort method = reader.ReadUInt16();
            if ((flags & 1) != 0 || (method != 0 && method != 8))
                throw new InvalidDataException("Encrypted or unsupported ZIP entry.");
            stream.Position += 4;
            crcs.Add(reader.ReadUInt32());
            stream.Position += 8;
            int nameSize = reader.ReadUInt16(), extraSize = reader.ReadUInt16(), commentSize = reader.ReadUInt16();
            stream.Position += 12L + nameSize + extraSize + commentSize;
            if (stream.Position > stream.Length) throw new InvalidDataException("Truncated ZIP directory.");
        }
        return crcs;
    }

    public static List<ResourceEntry> Plan(ZipArchive zip, List<uint> crcs)
    {
        if (zip.Entries.Count != crcs.Count) throw new InvalidDataException("ZIP directory mismatch.");
        string prefix = null;
        foreach (var entry in zip.Entries)
        {
            string name = SafeName(entry.FullName);
            string candidate = null;
            if (Selected(name)) candidate = "";
            else
            {
                int slash = name.IndexOf('/');
                if (slash >= 0 && slash + 1 < name.Length && Selected(name.Substring(slash + 1)))
                    candidate = name.Substring(0, slash + 1);
            }
            if (candidate != null)
            {
                if (prefix != null && prefix != candidate) throw new InvalidDataException("Ambiguous resource root in ZIP.");
                prefix = candidate;
            }
        }
        if (prefix == null) throw new InvalidDataException("The ZIP contains no SDK resources.");
        var plan = new List<ResourceEntry>();
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        bool settings = false, gamedata = false;
        for (int i = 0; i < zip.Entries.Count; ++i)
        {
            var entry = zip.Entries[i];
            string name = SafeName(entry.FullName);
            if (!name.StartsWith(prefix, StringComparison.Ordinal) || !Selected(name.Substring(prefix.Length))) continue;
            string relative = name.Substring(prefix.Length);
            // Reject Unix links and Windows reparse points before writing anything.
            int unixType = (entry.ExternalAttributes >> 16) & 0xf000;
            if ((unixType != 0 && unixType != 0x8000 && unixType != 0x4000) || (entry.ExternalAttributes & 0x400) != 0)
                throw new InvalidDataException("Links are not allowed in resource archives.");
            if (!names.Add(relative.TrimEnd('/'))) throw new InvalidDataException("Duplicate resource path in ZIP.");
            if (relative.StartsWith("settings/", StringComparison.OrdinalIgnoreCase)) settings = true;
            if (relative.StartsWith("gamedata/", StringComparison.OrdinalIgnoreCase)) gamedata = true;
            plan.Add(new ResourceEntry { Entry = entry, Path = relative, Crc = crcs[i] });
        }
        if (!settings || !gamedata) throw new InvalidDataException("The ZIP must contain settings and gamedata.");
        return plan;
    }

    static string Under(string root, string relative)
    {
        string result = System.IO.Path.GetFullPath(System.IO.Path.Combine(root, relative.Replace('/', '\\')));
        if (!result.StartsWith(root + "\\", StringComparison.OrdinalIgnoreCase)) throw new IOException("Resource path escaped its directory.");
        NoLinks(root, result);
        return result;
    }

    static void NoLinks(string root, string path)
    {
        for (string current = path; current != null && current.Length >= root.Length; current = System.IO.Path.GetDirectoryName(current))
        {
            try
            {
                // GetAttributes also detects dangling links, unlike File.Exists.
                if ((File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
                    throw new IOException("Resource paths cannot contain junctions or symbolic links.");
            }
            catch (FileNotFoundException) { }
            catch (DirectoryNotFoundException) { }
            if (String.Equals(current, root, StringComparison.OrdinalIgnoreCase)) return;
        }
        throw new IOException("Invalid resource directory.");
    }

    void CheckPause()
    {
        if (File.Exists(System.IO.Path.Combine(work, "pause"))) throw new Paused();
        try
        {
            using (var parent = Process.GetProcessById(parentId))
                if (!parent.HasExited && parent.StartTime.ToUniversalTime().ToFileTimeUtc() == parentStart) return;
        }
        catch (ArgumentException) { }
        catch (InvalidOperationException) { }
        throw new Paused();
    }

    static void AtomicText(string path, string text)
    {
        string temporary = path + ".tmp";
        using (var file = new FileStream(temporary, FileMode.Create, FileAccess.Write, FileShare.None))
        {
            byte[] bytes = Utf8.GetBytes(text);
            file.Write(bytes, 0, bytes.Length);
            file.Flush(true);
        }
        if (!MoveFileEx(temporary, path, 1 | 8)) throw new IOException("Cannot save resource progress.");
    }

    void Report(bool force)
    {
        if (!force && clock.Elapsed.TotalSeconds - lastReport < .2) return;
        AtomicText(System.IO.Path.Combine(work, "state.txt"), String.Join("\n", new[] {
            "resources-v1", phase, downloaded.ToString(CultureInfo.InvariantCulture), total.ToString(CultureInfo.InvariantCulture),
            rate.ToString(CultureInfo.InvariantCulture), extracted.ToString(CultureInfo.InvariantCulture),
            extractTotal.ToString(CultureInfo.InvariantCulture), message.Replace('\r', ' ').Replace('\n', ' '), FileId, target }));
        lastReport = clock.Elapsed.TotalSeconds;
    }

    static HttpWebResponse Request(string url, long start, bool probe, string validator)
    {
        var request = (HttpWebRequest)WebRequest.Create(url);
        request.UserAgent = "X-Ray-SDK-Resources/1.0";
        request.Timeout = 10000;
        request.ReadWriteTimeout = 5000;
        request.AllowAutoRedirect = true;
        request.MaximumAutomaticRedirections = 8;
        if (probe) request.AddRange(start, start);
        else request.AddRange(start);
        if (!String.IsNullOrEmpty(validator)) request.Headers["If-Range"] = validator;
        var response = (HttpWebResponse)request.GetResponse();
        string host = response.ResponseUri.Host;
        if (response.ResponseUri.Scheme != "https" || !(host == "google.com" || host.EndsWith(".google.com") ||
            host == "googleusercontent.com" || host.EndsWith(".googleusercontent.com")))
        { response.Dispose(); throw new IOException("Unexpected resource download redirect."); }
        return response;
    }

    static bool Html(HttpWebResponse response)
    {
        return (response.ContentType ?? "").StartsWith("text/html", StringComparison.OrdinalIgnoreCase);
    }

    Remote Probe()
    {
        string url = "https://drive.usercontent.google.com/download?id=" + FileId + "&export=download";
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            CheckPause();
            using (var response = Request(url, 0, true, null))
            {
                if (Html(response))
                {
                    string html;
                    using (var reader = new StreamReader(response.GetResponseStream(), Encoding.UTF8))
                    {
                        var chars = new char[65536];
                        int count = reader.ReadBlock(chars, 0, chars.Length);
                        html = new String(chars, 0, count);
                    }
                    var uuid = Regex.Match(html, "name=\"uuid\"\\s+value=\"([^\"]+)\"");
                    if (!html.Contains("id=\"download-form\"") || !uuid.Success)
                        throw new IOException("Google Drive denied the download. Check sharing or download quota.");
                    url = "https://drive.usercontent.google.com/download?id=" + FileId +
                        "&export=download&confirm=t&uuid=" + Uri.EscapeDataString(WebUtility.HtmlDecode(uuid.Groups[1].Value));
                    continue;
                }
                long size = 0;
                if (response.StatusCode == HttpStatusCode.PartialContent)
                {
                    var range = Regex.Match(response.Headers["Content-Range"] ?? "", @"^bytes 0-0/(\d+)$");
                    if (!range.Success || !Int64.TryParse(range.Groups[1].Value, out size))
                        throw new IOException("Invalid resource size response.");
                }
                else if (response.StatusCode == HttpStatusCode.OK) size = response.ContentLength;
                else throw new IOException("Google Drive returned an unexpected response.");
                if (size <= 0) throw new IOException("The resource archive size is unknown.");
                return new Remote { Url = url, Size = size, Validator = response.Headers["ETag"] ?? response.Headers["Last-Modified"] ?? "" };
            }
        }
        throw new IOException("Google Drive download confirmation failed.");
    }

    void Space(long archiveRemaining, long extractionRemaining)
    {
        string cacheVolume = System.IO.Path.GetPathRoot(work), sdkVolume = System.IO.Path.GetPathRoot(target);
        if (String.Equals(cacheVolume, sdkVolume, StringComparison.OrdinalIgnoreCase))
        {
            if (new DriveInfo(cacheVolume).AvailableFreeSpace < archiveRemaining + extractionRemaining + Reserve)
                throw new IOException("Not enough disk space for the archive and resources.");
        }
        else
        {
            if (new DriveInfo(cacheVolume).AvailableFreeSpace < archiveRemaining + Reserve ||
                new DriveInfo(sdkVolume).AvailableFreeSpace < extractionRemaining + Reserve)
                throw new IOException("Not enough disk space for the archive and resources.");
        }
    }

    long TreeSize(string root)
    {
        long size = 0;
        var pending = new Stack<string>();
        pending.Push(root);
        while (pending.Count > 0)
        {
            CheckPause();
            string directory = pending.Pop();
            NoLinks(root, directory);
            foreach (string file in Directory.GetFiles(directory))
            {
                NoLinks(root, file);
                if (System.IO.Path.GetFileName(file) != ".entry.part")
                    size = checked(size + new FileInfo(file).Length);
            }
            foreach (string child in Directory.GetDirectories(directory)) pending.Push(child);
        }
        return size;
    }

    long ExistingResourceBytes()
    {
        long existing = Directory.Exists(stage) ? TreeSize(stage) : 0;
        foreach (string folder in ResourceFolders)
        {
            string path = Under(target, folder);
            if (Directory.Exists(path)) existing = checked(existing + TreeSize(path));
        }
        return Math.Min(existing, KnownResourceSize);
    }

    public static bool ValidateResume(int status, string contentRange, long contentLength, long offset, long size)
    {
        if (status == 206)
        {
            var range = Regex.Match(contentRange ?? "", @"^bytes (\d+)-(\d+)/(\d+)$");
            long first, last, totalSize;
            if (!range.Success || !Int64.TryParse(range.Groups[1].Value, out first) ||
                !Int64.TryParse(range.Groups[2].Value, out last) || !Int64.TryParse(range.Groups[3].Value, out totalSize) ||
                first != offset || totalSize != size || last != size - 1 ||
                (contentLength >= 0 && contentLength != size - offset))
                throw new IOException("Invalid resume response. Partial data was preserved.");
            return true;
        }
        // A full body must replace, never append to, a partial file.
        if (status == 200 && contentLength == size) return false;
        throw new IOException("Unexpected archive response. Partial data was preserved.");
    }

    string Download()
    {
        phase = "checking";
        string partial = System.IO.Path.Combine(work, "archive.part"), archive = System.IO.Path.Combine(work, "archive.zip");
        string metaPath = System.IO.Path.Combine(work, "archive.meta");
        downloaded = File.Exists(archive) ? new FileInfo(archive).Length : File.Exists(partial) ? new FileInfo(partial).Length : 0;
        // A completed cache can be extracted offline. Partial downloads still need
        // a fresh remote validator before any bytes may be appended.
        if (File.Exists(archive) && File.Exists(metaPath))
        {
            string[] cached = File.ReadAllText(metaPath, Utf8).Split('\n');
            long cachedSize;
            if (cached.Length == 3 && cached[0] == FileId && Int64.TryParse(cached[1], out cachedSize) &&
                cachedSize > 0 && downloaded == cachedSize)
            {
                total = cachedSize;
                CheckPause();
                Report(true);
                return archive;
            }
        }
        Report(true);
        var remote = Probe();
        total = remote.Size;
        string metadata = FileId + "\n" + remote.Size.ToString(CultureInfo.InvariantCulture) + "\n" + remote.Validator;
        bool same = File.Exists(metaPath) && File.ReadAllText(metaPath, Utf8) == metadata && !String.IsNullOrEmpty(remote.Validator);
        if (same && File.Exists(archive) && new FileInfo(archive).Length == total)
        { downloaded = total; return archive; }
        if (File.Exists(archive)) File.Delete(archive);
        downloaded = File.Exists(partial) ? new FileInfo(partial).Length : 0;
        if (!same || downloaded > total)
        {
            if (File.Exists(partial)) File.Delete(partial);
            downloaded = 0;
        }
        // The observed public archive expands to about 2.8 GiB of selected resources.
        // Recheck the exact ZIP manifest before extraction; this is an early preflight.
        Space(total - downloaded, KnownResourceSize - ExistingResourceBytes());
        AtomicText(metaPath, metadata);
        if (downloaded < total)
        {
            CheckPause();
            using (var response = Request(remote.Url, downloaded, false, downloaded > 0 ? remote.Validator : null))
            {
                if (Html(response)) throw new IOException("Google Drive returned a web page instead of the archive.");
                if (!ValidateResume((int)response.StatusCode, response.Headers["Content-Range"], response.ContentLength, downloaded, total))
                    downloaded = 0;
                using (var input = response.GetResponseStream())
                using (var output = new FileStream(partial, downloaded == 0 ? FileMode.Create : FileMode.Open,
                    FileAccess.Write, FileShare.Read, 262144, FileOptions.SequentialScan))
                {
                    output.Position = downloaded;
                    var buffer = new byte[262144];
                    double sampleTime = clock.Elapsed.TotalSeconds;
                    long sampleBytes = downloaded;
                    phase = "downloading";
                    while (downloaded < total)
                    {
                        CheckPause();
                        int read = input.Read(buffer, 0, (int)Math.Min(buffer.Length, total - downloaded));
                        if (read == 0) throw new EndOfStreamException("Download interrupted. Resume to continue.");
                        output.Write(buffer, 0, read);
                        downloaded += read;
                        double now = clock.Elapsed.TotalSeconds;
                        if (now - sampleTime >= .2)
                        {
                            rate = (downloaded - sampleBytes) / (now - sampleTime);
                            output.Flush(true);
                            Report(true);
                            sampleTime = now;
                            sampleBytes = downloaded;
                        }
                    }
                    output.Flush(true);
                }
            }
        }
        CheckPause();
        if (new FileInfo(partial).Length != total) throw new IOException("Archive download is incomplete.");
        File.Move(partial, archive);
        rate = 0;
        return archive;
    }

    static readonly uint[] CrcTable = MakeCrcTable();
    static uint[] MakeCrcTable()
    {
        var table = new uint[256];
        for (uint i = 0; i < 256; ++i)
        {
            uint value = i;
            for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ ((value & 1) != 0 ? 0xedb88320U : 0U);
            table[i] = value;
        }
        return table;
    }
    static uint Crc(uint value, byte[] bytes, int count)
    {
        for (int i = 0; i < count; ++i) value = CrcTable[(value ^ bytes[i]) & 255] ^ (value >> 8);
        return value;
    }

    bool ValidStage(string path, ResourceEntry item)
    {
        if (!File.Exists(path) || new FileInfo(path).Length != item.Entry.Length) return false;
        uint crc = UInt32.MaxValue;
        using (var input = File.OpenRead(path))
        {
            var buffer = new byte[262144];
            int count;
            while ((count = input.Read(buffer, 0, buffer.Length)) > 0)
            { CheckPause(); crc = Crc(crc, buffer, count); Report(false); }
        }
        return (crc ^ UInt32.MaxValue) == item.Crc;
    }

    void Extract(string archive)
    {
        phase = "extracting";
        rate = 0;
        Report(true);
        using (var input = File.OpenRead(archive))
        {
            var crcs = ReadCrcs(input);
            input.Position = 0;
            using (var zip = new ZipArchive(input, ZipArchiveMode.Read, true))
            {
                var plan = Plan(zip, crcs);
                extractTotal = 0;
                foreach (var item in plan) extractTotal = checked(extractTotal + item.Entry.Length);
                Directory.CreateDirectory(stage);
                string partialEntry = Under(stage, ".entry.part");
                if (File.Exists(partialEntry)) File.Delete(partialEntry);
                long remaining = 0;
                foreach (var item in plan)
                {
                    if (item.Path.EndsWith("/")) continue;
                    string stagedFile = Under(stage, item.Path);
                    if (!File.Exists(Under(target, item.Path)) &&
                        (!File.Exists(stagedFile) || new FileInfo(stagedFile).Length != item.Entry.Length))
                        remaining = checked(remaining + item.Entry.Length);
                }
                Space(0, remaining);
                var buffer = new byte[262144];
                foreach (var item in plan)
                {
                    CheckPause();
                    string destination = Under(stage, item.Path.TrimEnd('/'));
                    string installed = Under(target, item.Path.TrimEnd('/'));
                    if (item.Path.EndsWith("/")) { Directory.CreateDirectory(destination); continue; }
                    // Preserve user resources, and skip files already committed before a pause.
                    if (File.Exists(installed) || ValidStage(destination, item))
                    { extracted += item.Entry.Length; Report(false); continue; }
                    if (File.Exists(destination)) File.Delete(destination);
                    Directory.CreateDirectory(System.IO.Path.GetDirectoryName(destination));
                    string temporary = System.IO.Path.Combine(stage, ".entry.part");
                    NoLinks(stage, temporary);
                    uint crc = UInt32.MaxValue;
                    long bytes = 0;
                    using (var source = item.Entry.Open())
                    using (var output = new FileStream(temporary, FileMode.Create, FileAccess.Write, FileShare.None))
                    {
                        int count;
                        while ((count = source.Read(buffer, 0, buffer.Length)) > 0)
                        {
                            CheckPause();
                            bytes += count;
                            if (bytes > item.Entry.Length) throw new InvalidDataException("ZIP file length mismatch.");
                            crc = Crc(crc, buffer, count);
                            output.Write(buffer, 0, count);
                            extracted += count;
                            Report(false);
                        }
                        output.Flush(true);
                    }
                    if (bytes != item.Entry.Length || (crc ^ UInt32.MaxValue) != item.Crc)
                    {
                        File.Delete(temporary);
                        throw new InvalidDataException("Resource CRC check failed. Remove the cached archive and retry.");
                    }
                    if (!MoveFileEx(temporary, destination, 1 | 8)) throw new IOException("Cannot stage resources. Check folder access.");
                }
            }
        }
        Install();
    }

    void Merge(string source, string destination)
    {
        CheckPause();
        NoLinks(stage, source);
        NoLinks(target, destination);
        if (!Directory.Exists(destination)) { Directory.Move(source, destination); return; }
        foreach (string file in Directory.GetFiles(source))
        {
            CheckPause();
            string output = System.IO.Path.Combine(destination, System.IO.Path.GetFileName(file));
            NoLinks(stage, file);
            NoLinks(target, output);
            if (!File.Exists(output)) File.Move(file, output);
        }
        foreach (string directory in Directory.GetDirectories(source))
            Merge(directory, System.IO.Path.Combine(destination, System.IO.Path.GetFileName(directory)));
    }

    void Install()
    {
        phase = "installing";
        Report(true);
        // Keep a required folder absent until the very last commit, so a crash or
        // pause cannot unlock editors while the resource set is only partly installed.
        string last = !IsDirectory(System.IO.Path.Combine(target, "gamedata")) ? "gamedata" : "settings";
        foreach (string folder in ResourceFolders)
            if (folder != last && Directory.Exists(System.IO.Path.Combine(stage, folder)))
                Merge(System.IO.Path.Combine(stage, folder), System.IO.Path.Combine(target, folder));
        if (Directory.Exists(System.IO.Path.Combine(stage, last)))
            Merge(System.IO.Path.Combine(stage, last), System.IO.Path.Combine(target, last));
        if (!IsDirectory(System.IO.Path.Combine(target, "settings")) || !IsDirectory(System.IO.Path.Combine(target, "gamedata")))
            throw new IOException("Resource installation did not create the required folders.");
        phase = "complete";
        Report(true);
        // Only clean owned paths after success. Never traverse reparse points.
        try
        {
            DeleteStage(stage);
            File.Delete(System.IO.Path.Combine(work, "archive.zip"));
            File.Delete(System.IO.Path.Combine(work, "archive.meta"));
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }

    void DeleteStage(string path)
    {
        NoLinks(stage, path);
        foreach (string file in Directory.GetFiles(path))
        { NoLinks(stage, file); File.Delete(file); }
        foreach (string directory in Directory.GetDirectories(path)) DeleteStage(directory);
        Directory.Delete(path);
    }

    public static int Run(string target, string work, int parentId, long parentStart)
    {
        ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
        var worker = new SDKResourceWorker { target = Root(target), work = Root(work), parentId = parentId, parentStart = parentStart };
        worker.stage = System.IO.Path.Combine(worker.target, ".sdk-resource-stage-" + FileId);
        // A second launcher must not write into an active worker's cache/staging.
        FileStream installationLock;
        try { installationLock = new FileStream(System.IO.Path.Combine(worker.work, "worker.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None); }
        catch (IOException) { return 2; }
        using (installationLock)
        {
            try
            {
                NoLinks(worker.work, worker.work);
                NoLinks(worker.target, worker.stage);
                worker.CheckPause();
                string archive = worker.Download();
                worker.Extract(archive);
                return 0;
            }
            catch (Paused)
            {
                worker.phase = "paused";
                worker.rate = 0;
                worker.Report(true);
                return 0;
            }
            catch (Exception error)
            {
                if (File.Exists(System.IO.Path.Combine(worker.work, "pause")))
                {
                    worker.phase = "paused";
                    worker.rate = 0;
                    worker.Report(true);
                    return 0;
                }
                worker.phase = "error";
                worker.rate = 0;
                worker.message = error.Message;
                if (error is InvalidDataException)
                {
                    // Never resume a known-corrupt ZIP. Verified staging files are retained
                    // and checked against the next archive before reuse.
                    try
                    {
                        File.Delete(System.IO.Path.Combine(worker.work, "archive.zip"));
                        File.Delete(System.IO.Path.Combine(worker.work, "archive.meta"));
                    }
                    catch (IOException) { }
                    catch (UnauthorizedAccessException) { }
                    worker.message = "Invalid resource archive. Resume to download it again.";
                }
                File.WriteAllText(System.IO.Path.Combine(worker.work, "worker-error.txt"), error.ToString());
                try { worker.Report(true); } catch (IOException) { }
                return 1;
            }
        }
    }
}
