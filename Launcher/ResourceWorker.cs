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
    const string SourceUrl = "https://drive.usercontent.google.com/download?id=" + FileId + "&export=download&authuser=0";
    const long KnownArchiveSize = 11450714965L;
    const long KnownResourceSize = 2998494318L;
    const long Reserve = 64L * 1024 * 1024;
    static readonly string[] ResourceFolders = { "rawdata", "groups", "maps", "settings", "gamedata" };
    static readonly Encoding Utf8 = new UTF8Encoding(false, true);
    string target, work, cache, previousCache, stage, phase = "checking", message = "";
    int parentId;
    long parentStart, downloaded, total = KnownArchiveSize, extracted, extractTotal;
    double rate;
    readonly Stopwatch clock = Stopwatch.StartNew();
    readonly CookieContainer cookies = new CookieContainer();
    double lastReport;

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool MoveFileEx(string source, string destination, int flags);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool GetDiskFreeSpaceEx(string directory, out ulong available, out ulong total, out ulong free);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool GetVolumePathName(string path, StringBuilder volume, int length);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool GetVolumeNameForVolumeMountPoint(string mount, StringBuilder name, int length);

    static long FreeSpace(string path)
    {
        ulong available, total, free;
        if (!GetDiskFreeSpaceEx(path, out available, out total, out free)) throw new IOException("Cannot check free disk space.");
        return available > Int64.MaxValue ? Int64.MaxValue : (long)available;
    }

    static string Volume(string path)
    {
        var volume = new StringBuilder(32768);
        if (!GetVolumePathName(path, volume, volume.Capacity)) throw new IOException("Cannot identify the resource disk.");
        var name = new StringBuilder(1024);
        if (GetVolumeNameForVolumeMountPoint(volume.ToString(), name, name.Capacity)) return name.ToString();
        return volume.ToString();
    }

    sealed class Paused : Exception { }
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

    HttpWebResponse Request(string url, long start, string validator)
    {
        var request = (HttpWebRequest)WebRequest.Create(url);
        request.UserAgent = "X-Ray-SDK-Resources/1.0";
        request.Timeout = 10000;
        request.ReadWriteTimeout = 5000;
        request.AllowAutoRedirect = true;
        request.MaximumAutomaticRedirections = 8;
        // Confirmation and subsequent range requests must share the Drive session.
        request.CookieContainer = cookies;
        request.Accept = "*/*";
        request.Referer = "https://drive.google.com/file/d/" + FileId + "/view";
        // A fresh download is a normal browser-style GET, without Range: bytes=0-0
        // or bytes=0-. Use ranges only for an actual saved partial download.
        if (start > 0) request.AddRange(start);
        if (!String.IsNullOrEmpty(validator)) request.Headers["If-Range"] = validator;
        HttpWebResponse response;
        try { response = (HttpWebResponse)request.GetResponse(); }
        catch (WebException failure)
        {
            response = failure.Response as HttpWebResponse;
            if (response != null && response.StatusCode == HttpStatusCode.RequestedRangeNotSatisfiable && start > 0)
            {
                response.Dispose();
                return Request(url, 0, null);
            }
            // Quota/access errors often arrive as an HTML body with HTTP 403.
            if (response == null || !Html(response))
            {
                if (response != null) response.Dispose();
                throw;
            }
        }
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

    static string HtmlAttribute(string tag, string name)
    {
        var match = Regex.Match(tag, @"(?:^|\s)" + Regex.Escape(name) +
            @"\s*=\s*(?:""([^""]*)""|'([^']*)'|([^\s>]+))", RegexOptions.IgnoreCase);
        if (!match.Success) return "";
        for (int i = 1; i <= 3; ++i)
            if (match.Groups[i].Success) return WebUtility.HtmlDecode(match.Groups[i].Value);
        return "";
    }

    public static string ConfirmationUrl(string html)
    {
        foreach (Match form in Regex.Matches(html, @"<form\b[^>]*>.*?</form\s*>", RegexOptions.IgnoreCase | RegexOptions.Singleline))
        {
            string opening = form.Value.Substring(0, form.Value.IndexOf('>') + 1);
            if (HtmlAttribute(opening, "id") != "download-form") continue;
            string action = HtmlAttribute(opening, "action");
            Uri actionUri;
            if (!String.IsNullOrEmpty(action) && action[0] != '/' &&
                (!Uri.TryCreate(action, UriKind.Absolute, out actionUri) || actionUri.Scheme != "https" ||
                    actionUri.Host != "drive.usercontent.google.com" || actionUri.AbsolutePath != "/download"))
                throw new IOException("Unexpected Google Drive confirmation target.");
            string id = "", confirm = "", uuid = "";
            foreach (Match input in Regex.Matches(form.Value, @"<input\b[^>]*>", RegexOptions.IgnoreCase))
            {
                string name = HtmlAttribute(input.Value, "name"), value = HtmlAttribute(input.Value, "value");
                if (name == "id") id = value;
                else if (name == "confirm") confirm = value;
                else if (name == "uuid") uuid = value;
            }
            if ((!String.IsNullOrEmpty(id) && id != FileId) || String.IsNullOrEmpty(confirm))
                throw new IOException("Invalid Google Drive download confirmation.");
            return "https://drive.usercontent.google.com/download?id=" + FileId + "&export=download&confirm=" +
                Uri.EscapeDataString(confirm) + (String.IsNullOrEmpty(uuid) ? "" : "&uuid=" + Uri.EscapeDataString(uuid)) + "&authuser=0";
        }
        if (Regex.IsMatch(html, "too many users|download quota|quota exceeded", RegexOptions.IgnoreCase))
            throw new IOException("Google Drive quota exceeded for this request. Try again later.");
        throw new IOException("Google Drive denied the download. See drive-response.html in the cache.");
    }

    HttpWebResponse OpenArchive(string url, long start, string validator)
    {
        // Handle the same confirmation form as the browser, including expired tokens.
        for (int attempt = 0; attempt < 5; ++attempt)
        {
            CheckPause();
            var response = Request(url, start, validator);
            if (!Html(response)) return response;
            string html;
            using (response)
            using (var reader = new StreamReader(response.GetResponseStream(), Encoding.UTF8))
            {
                var chars = new char[65536];
                int count = reader.ReadBlock(chars, 0, chars.Length);
                html = new String(chars, 0, count);
            }
            File.WriteAllText(System.IO.Path.Combine(work, "drive-response.html"), html, Utf8);
            try { url = ConfirmationUrl(html); }
            catch (IOException)
            {
                // Drive can reject a range request while serving an ordinary GET.
                // Try that once; keep the old partial untouched until a valid body arrives.
                if (start <= 0) throw;
                start = 0;
                validator = null;
                url = SourceUrl;
            }
        }
        throw new IOException("Google Drive keeps requesting confirmation. Try Resume later.");
    }

    public static long ResponseSize(int status, string contentRange, long contentLength)
    {
        long size = 0;
        if (status == 206)
        {
            var range = Regex.Match(contentRange ?? "", @"^bytes \d+-\d+/(\d+)$");
            if (!range.Success || !Int64.TryParse(range.Groups[1].Value, out size))
                throw new IOException("Invalid resource size response.");
        }
        else if (status == 200) size = contentLength;
        else throw new IOException("Google Drive returned an unexpected response.");
        if (size <= 0) throw new IOException("The resource archive size is unknown.");
        return size;
    }

    void Space(long archiveRemaining, long extractionRemaining)
    {
        string cacheVolume = Volume(cache), sdkVolume = Volume(target);
        if (String.Equals(cacheVolume, sdkVolume, StringComparison.OrdinalIgnoreCase))
        {
            if (FreeSpace(cache) < archiveRemaining + extractionRemaining + Reserve)
                throw new IOException("Not enough disk space for the archive and resources.");
        }
        else
        {
            if (FreeSpace(cache) < archiveRemaining + Reserve)
                throw new IOException("Not enough space in TEMP. Choose another TEMP folder.");
            if (FreeSpace(target) < extractionRemaining + Reserve)
                throw new IOException("Not enough space on the SDK disk to install resources.");
        }
    }

    void RelocateArchive()
    {
        if (String.IsNullOrEmpty(previousCache) || String.Equals(previousCache, cache, StringComparison.OrdinalIgnoreCase) ||
            !Directory.Exists(previousCache)) return;
        NoLinks(previousCache, previousCache);
        string name = File.Exists(Under(previousCache, "archive.zip")) ? "archive.zip" : "archive.part";
        string source = Under(previousCache, name), sourceMeta = Under(previousCache, "archive.meta");
        if (!File.Exists(source) || !File.Exists(sourceMeta)) return;
        string metadata = File.ReadAllText(sourceMeta, Utf8);
        if (!metadata.StartsWith(FileId + "\n", StringComparison.Ordinal)) return;
        var info = new FileInfo(source);
        long size = info.Length;
        string fingerprint = source + "\n" + size.ToString(CultureInfo.InvariantCulture) + "\n" +
            info.LastWriteTimeUtc.Ticks.ToString(CultureInfo.InvariantCulture) + "\n" + metadata;
        string marker = Under(cache, "transfer.meta"), transfer = Under(cache, "archive.transfer");
        string destination = Under(cache, name), destinationMeta = Under(cache, "archive.meta");
        bool sameTransfer = File.Exists(marker) && File.ReadAllText(marker, Utf8) == fingerprint;
        if (File.Exists(destination))
        {
            // A previous copy may have finished just before the launcher closed.
            if (!sameTransfer || new FileInfo(destination).Length != size || !File.Exists(destinationMeta) ||
                File.ReadAllText(destinationMeta, Utf8) != metadata) return;
        }
        else
        {
            // Keep a valid cache at the chosen destination instead of replacing it.
            string other = Under(cache, name == "archive.zip" ? "archive.part" : "archive.zip");
            if (File.Exists(other)) return;
            long copied = sameTransfer && File.Exists(transfer) ? new FileInfo(transfer).Length : 0;
            if (!sameTransfer || copied > size)
            {
                if (File.Exists(transfer)) File.Delete(transfer);
                copied = 0;
            }
            if (FreeSpace(cache) < size - copied + Reserve)
                throw new IOException("Not enough space to move the archive into the selected TEMP folder.");
            AtomicText(destinationMeta, metadata);
            AtomicText(marker, fingerprint);
            phase = "moving";
            downloaded = size;
            string[] sourceDescription = metadata.Split('\n');
            long sourceSize;
            if (sourceDescription.Length == 3 && Int64.TryParse(sourceDescription[1], out sourceSize) && sourceSize >= size)
                total = sourceSize;
            Report(true);
            using (var input = new FileStream(source, FileMode.Open, FileAccess.Read, FileShare.Read))
            using (var output = new FileStream(transfer, FileMode.OpenOrCreate, FileAccess.Write, FileShare.Read))
            {
                input.Position = output.Position = copied;
                var buffer = new byte[262144];
                double sampleTime = clock.Elapsed.TotalSeconds;
                long sampleBytes = copied;
                while (copied < size)
                {
                    CheckPause();
                    int read = input.Read(buffer, 0, (int)Math.Min(buffer.Length, size - copied));
                    if (read == 0) throw new EndOfStreamException("Cached archive changed while moving TEMP.");
                    output.Write(buffer, 0, read);
                    copied += read;
                    double now = clock.Elapsed.TotalSeconds;
                    if (now - sampleTime >= .2)
                    {
                        rate = (copied - sampleBytes) / (now - sampleTime);
                        output.Flush(true);
                        Report(true);
                        sampleBytes = copied; sampleTime = now;
                    }
                }
                output.Flush(true);
            }
            CheckPause();
            if (!MoveFileEx(transfer, destination, 8)) throw new IOException("Cannot finish moving the cached archive.");
        }
        // The original is retained until a complete destination copy is committed.
        CheckPause();
        File.Delete(source);
        if (!File.Exists(Under(previousCache, "archive.part")) && !File.Exists(Under(previousCache, "archive.zip"))) File.Delete(sourceMeta);
        File.Delete(marker);
        rate = 0;
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
        string partial = Under(cache, "archive.part"), archive = Under(cache, "archive.zip");
        string metaPath = Under(cache, "archive.meta");
        downloaded = File.Exists(archive) ? new FileInfo(archive).Length : File.Exists(partial) ? new FileInfo(partial).Length : 0;
        string[] cached = File.Exists(metaPath) ? File.ReadAllText(metaPath, Utf8).Split('\n') : new string[0];
        long cachedSize = 0;
        bool validMetadata = cached.Length == 3 && cached[0] == FileId &&
            Int64.TryParse(cached[1], out cachedSize) && cachedSize > 0;
        // Completed caches, including a fully flushed partial left by a pause just
        // before the final rename, can be extracted without contacting Drive again.
        if (validMetadata)
        {
            string complete = File.Exists(archive) && new FileInfo(archive).Length == cachedSize ? archive :
                File.Exists(partial) && new FileInfo(partial).Length == cachedSize ? partial : null;
            if (complete != null)
            {
                total = cachedSize;
                downloaded = total;
                CheckPause();
                if (complete == partial)
                {
                    if (File.Exists(archive)) File.Delete(archive);
                    File.Move(partial, archive);
                }
                Report(true);
                return archive;
            }
        }
        Report(true);
        long partialSize = File.Exists(partial) ? new FileInfo(partial).Length : 0;
        long offset = validMetadata && !String.IsNullOrEmpty(cached[2]) && partialSize < cachedSize ? partialSize : 0;
        string validator = offset > 0 ? cached[2] : null;
        // Obtain size, identity and data from one response. A separate byte-zero
        // probe needlessly consumes a Drive request/confirmation and may be denied
        // even when the normal browser download works.
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            CheckPause();
            using (var response = OpenArchive(SourceUrl, offset, validator))
            {
                long size = ResponseSize((int)response.StatusCode, response.Headers["Content-Range"], response.ContentLength);
                string identity = response.Headers["ETag"] ?? response.Headers["Last-Modified"] ?? "";
                bool ranged = ValidateResume((int)response.StatusCode, response.Headers["Content-Range"], response.ContentLength, offset, size);
                if (ranged && offset > 0 && (size != cachedSize || String.IsNullOrEmpty(identity) || identity != validator))
                {
                    // Do not append changed remote data, even if a server ignored If-Range.
                    offset = 0;
                    validator = null;
                    continue;
                }
                long writeOffset = ranged ? offset : 0;
                Space(Math.Max(0, size - writeOffset - (writeOffset == 0 ? partialSize : 0)),
                    KnownResourceSize - ExistingResourceBytes());
                CheckPause();
                // Only modify the old cache once the response is a validated archive
                // body. HTML quota/confirmation errors leave partial data intact.
                if (File.Exists(archive)) File.Delete(archive);
                total = size;
                downloaded = writeOffset;
                using (var input = response.GetResponseStream())
                using (var output = new FileStream(partial, writeOffset == 0 ? FileMode.Create : FileMode.Open,
                    FileAccess.Write, FileShare.Read, 262144, FileOptions.SequentialScan))
                {
                    // Truncate a restarted partial before committing its new identity;
                    // a crash must never associate old bytes with a new remote validator.
                    AtomicText(metaPath, FileId + "\n" + size.ToString(CultureInfo.InvariantCulture) + "\n" + identity);
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
            CheckPause();
            if (new FileInfo(partial).Length != total) throw new IOException("Archive download is incomplete.");
            File.Move(partial, archive);
            rate = 0;
            return archive;
        }
        throw new IOException("The resource archive changed. Resume to retry.");
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
            File.Delete(Under(cache, "archive.zip"));
            File.Delete(Under(cache, "archive.meta"));
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
        return Run(target, work, work, "", parentId, parentStart);
    }

    public static int Run(string target, string work, string archiveDirectory, string previousArchiveDirectory, int parentId, long parentStart)
    {
        ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
        var worker = new SDKResourceWorker { target = Root(target), work = Root(work), cache = Root(archiveDirectory),
            previousCache = String.IsNullOrEmpty(previousArchiveDirectory) ? "" : Root(previousArchiveDirectory), parentId = parentId, parentStart = parentStart };
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
                Directory.CreateDirectory(worker.cache);
                NoLinks(worker.cache, worker.cache);
                worker.RelocateArchive();
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
                        File.Delete(Under(worker.cache, "archive.zip"));
                        File.Delete(Under(worker.cache, "archive.meta"));
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
