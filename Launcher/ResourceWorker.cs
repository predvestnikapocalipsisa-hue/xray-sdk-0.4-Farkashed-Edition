using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Net;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;

public sealed class SDKResourceWorker
{
    public const string SourceId = "github:lunardontcry/SDK-DATA-PASS:7z-v1";
    const string Repository = "lunardontcry/SDK-DATA-PASS";
    const string Api = "https://api.github.com/repos/" + Repository + "/releases";
    const long Reserve = 64L * 1024 * 1024;
    const long ResourceEstimate = 2998494318L;
    static readonly Encoding Utf8 = new UTF8Encoding(false, true);
    static readonly string[] Folders = { "rawdata", "groups", "maps", "settings", "gamedata" };
    const string NestedInGame = "bins/InGame";
    string target, sdk, work, cache, previousCache, stage, phase = "checking", message = "";
    int parentId;
    long parentStart, downloaded, total = 10123443705L, extracted, extractTotal;
    double rate, lastReport;
    readonly Stopwatch clock = Stopwatch.StartNew();
    readonly JavaScriptSerializer json = new JavaScriptSerializer { MaxJsonLength = 16777216 };
    List<Part> parts;
    sealed class Paused : Exception { }
    public sealed class Part
    {
        public string Name, Url, Sha256;
        public long Id, Size;
        public int Number;
    }
    public sealed class Manifest
    {
        public string Source;
        public List<Part> Parts;
    }
    public sealed class Entry
    {
        public string Name, Relative;
        public long Size;
        public bool Directory;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool MoveFileEx(string source, string destination, int flags);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool GetDiskFreeSpaceEx(string path, out ulong available, out ulong total, out ulong free);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool GetVolumePathName(string path, StringBuilder volume, int length);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool GetVolumeNameForVolumeMountPoint(string mount, StringBuilder name, int length);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr CreateJobObject(IntPtr attributes, string name);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool SetInformationJobObject(IntPtr job, int kind, IntPtr info, uint length);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr handle);
    [StructLayout(LayoutKind.Sequential)]
    struct BasicLimits
    {
        public long ProcessTime, JobTime;
        public uint Flags;
        public UIntPtr MinWorkingSet, MaxWorkingSet;
        public uint ActiveProcesses;
        public UIntPtr Affinity;
        public uint Priority, Scheduling;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct IoCounters { public ulong ReadOps, WriteOps, OtherOps, ReadBytes, WriteBytes, OtherBytes; }
    [StructLayout(LayoutKind.Sequential)]
    struct ExtendedLimits
    {
        public BasicLimits Basic;
        public IoCounters Io;
        public UIntPtr ProcessMemory, JobMemory, PeakProcessMemory, PeakJobMemory;
    }

    static string Root(string value) { return Path.GetFullPath(value).TrimEnd('\\', '/'); }
    public static string SafeName(string name)
    {
        name = name.Replace('\\', '/');
        if (name.Length == 0 || name[0] == '/' || name.IndexOf(':') >= 0)
            throw new InvalidDataException("Unsafe archive path.");
        foreach (string part in name.TrimEnd('/').Split('/'))
            if (part.Length == 0 || part == "." || part == ".." || part.EndsWith(".") || part.EndsWith(" ") ||
                part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||
                Regex.IsMatch(part, @"^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\.|$)", RegexOptions.IgnoreCase))
                throw new InvalidDataException("Unsafe archive path.");
        return name;
    }
    static void NoLinks(string root, string path)
    {
        for (string current = path; current != null && current.Length >= root.Length; current = Path.GetDirectoryName(current))
        {
            try
            {
                if ((File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
                    throw new IOException("Resource paths cannot contain links or junctions.");
            }
            catch (FileNotFoundException) { }
            catch (DirectoryNotFoundException) { }
            if (String.Equals(current, root, StringComparison.OrdinalIgnoreCase)) return;
        }
        throw new IOException("Resource path escaped its directory.");
    }
    static string Under(string root, string relative)
    {
        string path = Path.GetFullPath(Path.Combine(root, relative.Replace('/', '\\')));
        if (!path.StartsWith(root + "\\", StringComparison.OrdinalIgnoreCase)) throw new IOException("Invalid resource path.");
        NoLinks(root, path);
        return path;
    }
    static long Free(string path)
    {
        ulong available, size, free;
        if (!GetDiskFreeSpaceEx(path, out available, out size, out free)) throw new IOException("Cannot check free disk space.");
        return available > Int64.MaxValue ? Int64.MaxValue : (long)available;
    }
    static string Volume(string path)
    {
        var mount = new StringBuilder(32768);
        var name = new StringBuilder(1024);
        if (!GetVolumePathName(path, mount, mount.Capacity)) throw new IOException("Cannot identify the resource disk.");
        return GetVolumeNameForVolumeMountPoint(mount.ToString(), name, name.Capacity) ? name.ToString() : mount.ToString();
    }
    void Space(long archiveBytes, long resourceBytes)
    {
        if (String.Equals(Volume(cache), Volume(target), StringComparison.OrdinalIgnoreCase))
        {
            if (Free(cache) < archiveBytes + resourceBytes + Reserve) throw new IOException("Not enough disk space for parts and resources.");
        }
        else
        {
            if (Free(cache) < archiveBytes + Reserve) throw new IOException("Not enough space in TEMP. Choose another TEMP folder.");
            if (Free(target) < resourceBytes + Reserve) throw new IOException("Not enough space on the SDK disk for resources.");
        }
    }
    long ExistingResourceBytes()
    {
        long bytes = 0;
        var pending = new Stack<string>();
        foreach (string folder in Folders) if (Directory.Exists(Under(target, folder))) pending.Push(Under(target, folder));
        while (pending.Count > 0)
        {
            CheckPause();
            string directory = pending.Pop(); NoLinks(target, directory);
            foreach (string file in Directory.GetFiles(directory)) { NoLinks(target, file); bytes = checked(bytes + new FileInfo(file).Length); }
            foreach (string child in Directory.GetDirectories(directory)) pending.Push(child);
        }
        return Math.Min(bytes, ResourceEstimate);
    }
    void CheckPause()
    {
        if (File.Exists(Path.Combine(work, "pause"))) throw new Paused();
        try
        {
            using (var parent = Process.GetProcessById(parentId))
                if (!parent.HasExited && parent.StartTime.ToUniversalTime().ToFileTimeUtc() == parentStart) return;
        }
        catch (ArgumentException) { }
        catch (InvalidOperationException) { }
        throw new Paused();
    }
    static void AtomicText(string path, string value)
    {
        using (var output = new FileStream(path + ".tmp", FileMode.Create, FileAccess.Write, FileShare.None))
        {
            byte[] data = Utf8.GetBytes(value);
            output.Write(data, 0, data.Length);
            output.Flush(true);
        }
        if (!MoveFileEx(path + ".tmp", path, 1 | 8)) throw new IOException("Cannot save resource progress.");
    }
    void Report(bool force)
    {
        if (!force && clock.Elapsed.TotalSeconds - lastReport < .2) return;
        AtomicText(Path.Combine(work, "state.txt"), String.Join("\n", new[] {
            "resources-v2", phase, downloaded.ToString(CultureInfo.InvariantCulture), total.ToString(CultureInfo.InvariantCulture),
            rate.ToString(CultureInfo.InvariantCulture), extracted.ToString(CultureInfo.InvariantCulture), extractTotal.ToString(CultureInfo.InvariantCulture),
            message.Replace('\r', ' ').Replace('\n', ' '), SourceId, target }));
        lastReport = clock.Elapsed.TotalSeconds;
    }

    public static void ValidateParts(List<Part> list)
    {
        if (list == null || list.Count == 0 || list.Count > 999) throw new InvalidDataException("No split 7z archive found in GitHub releases.");
        list.Sort((a, b) => a.Number.CompareTo(b.Number));
        string archive = null;
        long sum = 0;
        for (int i = 0; i < list.Count; ++i)
        {
            var part = list[i];
            var match = Regex.Match(SafeName(part.Name), @"^([^/]+\.7z)\.(\d{3})$", RegexOptions.IgnoreCase);
            Uri uri;
            if (!match.Success || part.Number != i + 1 || Int32.Parse(match.Groups[2].Value, CultureInfo.InvariantCulture) != part.Number ||
                part.Size <= 0 || part.Id <= 0 || !Regex.IsMatch(part.Sha256 ?? "", @"\A[0-9a-fA-F]{64}\z") ||
                !Uri.TryCreate(part.Url, UriKind.Absolute, out uri) || uri.Scheme != "https" || uri.Host != "github.com" ||
                !uri.AbsolutePath.StartsWith("/" + Repository + "/releases/download/", StringComparison.Ordinal) ||
                Uri.UnescapeDataString(Path.GetFileName(uri.AbsolutePath)) != part.Name)
                throw new InvalidDataException("Invalid or missing archive part in GitHub releases.");
            if (archive != null && archive != match.Groups[1].Value) throw new InvalidDataException("Mixed split archives in the manifest.");
            archive = match.Groups[1].Value;
            sum = checked(sum + part.Size);
        }
    }
    public static List<Part> ParseReleases(string text)
    {
        var serializer = new JavaScriptSerializer { MaxJsonLength = 16777216 };
        var releases = serializer.DeserializeObject(text) as object[];
        if (releases == null) throw new InvalidDataException("Invalid GitHub releases response.");
        var groups = new Dictionary<string, Dictionary<int, Part>>(StringComparer.Ordinal);
        string selected = null;
        foreach (Dictionary<string, object> release in releases)
        {
            if (Convert.ToBoolean(release["draft"], CultureInfo.InvariantCulture) || Convert.ToBoolean(release["prerelease"], CultureInfo.InvariantCulture)) continue;
            foreach (Dictionary<string, object> asset in (object[])release["assets"])
            {
                string name = Convert.ToString(asset["name"], CultureInfo.InvariantCulture);
                var match = Regex.Match(name, @"^([^/\\]+\.7z)\.(\d{3})$", RegexOptions.IgnoreCase);
                if (!match.Success) continue;
                string group = match.Groups[1].Value;
                if (selected == null) selected = group;
                if (group != selected) continue;
                Dictionary<int, Part> indexed;
                if (!groups.TryGetValue(group, out indexed)) groups[group] = indexed = new Dictionary<int, Part>();
                object digest;
                string hash = asset.TryGetValue("digest", out digest) ? Convert.ToString(digest, CultureInfo.InvariantCulture) : "";
                if (!hash.StartsWith("sha256:", StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("GitHub archive parts must have SHA-256 digests.");
                var part = new Part { Name = name, Number = Int32.Parse(match.Groups[2].Value, CultureInfo.InvariantCulture),
                    Id = Convert.ToInt64(asset["id"], CultureInfo.InvariantCulture), Size = Convert.ToInt64(asset["size"], CultureInfo.InvariantCulture),
                    Url = Convert.ToString(asset["browser_download_url"], CultureInfo.InvariantCulture), Sha256 = hash.Substring(7).ToLowerInvariant() };
                Part older;
                if (!indexed.TryGetValue(part.Number, out older) || part.Id > older.Id) indexed[part.Number] = part;
            }
        }
        var result = selected == null ? new List<Part>() : new List<Part>(groups[selected].Values);
        ValidateParts(result);
        return result;
    }
    static bool AllowedHost(string host)
    {
        return host == "github.com" || host == "api.github.com" || host.EndsWith(".githubusercontent.com", StringComparison.OrdinalIgnoreCase);
    }
    HttpWebResponse Request(string url, long offset, bool api)
    {
        var uri = new Uri(url);
        if (uri.Scheme != "https" || !AllowedHost(uri.Host)) throw new IOException("Unexpected GitHub download URL.");
        var request = (HttpWebRequest)WebRequest.Create(uri);
        request.UserAgent = "X-Ray-SDK-Resources/2.0";
        request.Accept = api ? "application/vnd.github+json" : "application/octet-stream";
        request.Timeout = 15000; request.ReadWriteTimeout = 5000;
        request.MaximumAutomaticRedirections = 8;
        if (offset > 0) request.AddRange(offset);
        HttpWebResponse response;
        try { response = (HttpWebResponse)request.GetResponse(); }
        catch (WebException error)
        {
            var failed = error.Response as HttpWebResponse;
            if (failed != null)
            {
                var status = failed.StatusCode;
                failed.Dispose();
                if (status == HttpStatusCode.RequestedRangeNotSatisfiable && offset > 0) return Request(url, 0, api);
                if (status == HttpStatusCode.Forbidden || (int)status == 429) throw new IOException("GitHub rejected the request or its API limit was reached. Resume later.", error);
            }
            throw;
        }
        if (response.ResponseUri.Scheme != "https" || !AllowedHost(response.ResponseUri.Host))
        { response.Dispose(); throw new IOException("Unexpected GitHub redirect."); }
        return response;
    }
    List<Part> LoadManifest()
    {
        string path = Path.Combine(work, "manifest.json");
        if (File.Exists(path))
        {
            try
            {
                var old = json.Deserialize<Manifest>(File.ReadAllText(path, Utf8));
                if (old == null) throw new InvalidDataException("Invalid cached manifest.");
                ValidateParts(old.Parts);
                if (old.Source == SourceId)
                {
                    bool complete = true;
                    foreach (var part in old.Parts) if (!File.Exists(Under(cache, part.Name)) || new FileInfo(Under(cache, part.Name)).Length != part.Size) complete = false;
                    if (complete) return old.Parts; // completed downloads can be installed offline
                }
            }
            catch (ArgumentException) { }
            catch (InvalidDataException) { }
        }
        var releases = new List<object>();
        for (int page = 1; page <= 10; ++page)
        {
            CheckPause();
            using (var response = Request(Api + "?per_page=100&page=" + page, 0, true))
            using (var reader = new StreamReader(response.GetResponseStream(), Utf8))
            {
                var chars = new char[16777217];
                int length = reader.ReadBlock(chars, 0, chars.Length);
                if (length == chars.Length) throw new IOException("GitHub releases response is too large.");
                var batch = json.DeserializeObject(new String(chars, 0, length)) as object[];
                if (batch == null) throw new IOException("Invalid GitHub releases response.");
                releases.AddRange(batch);
                if (batch.Length < 100)
                {
                    var result = ParseReleases(json.Serialize(releases));
                    AtomicText(path, json.Serialize(new Manifest { Source = SourceId, Parts = result }));
                    return result;
                }
            }
        }
        throw new IOException("Too many GitHub release pages to select an archive safely.");
    }
    static string Identity(Part part) { return SourceId + "\n" + part.Id + "\n" + part.Size + "\n" + part.Sha256; }
    bool Matches(string root, Part part)
    {
        string meta = Under(root, part.Name + ".meta");
        return File.Exists(meta) && File.ReadAllText(meta, Utf8) == Identity(part);
    }
    long CachedBytes()
    {
        long bytes = 0;
        foreach (var part in parts)
        {
            if (!Matches(cache, part)) continue;
            string full = Under(cache, part.Name), partial = Under(cache, part.Name + ".part");
            long size = File.Exists(full) ? new FileInfo(full).Length : File.Exists(partial) ? new FileInfo(partial).Length : 0;
            bytes = checked(bytes + Math.Min(size, part.Size));
        }
        return bytes;
    }
    bool Verify(string path, Part part)
    {
        if (!File.Exists(path) || new FileInfo(path).Length != part.Size) return false;
        phase = "verifying"; rate = 0; Report(true);
        using (var input = File.OpenRead(path))
        using (var hash = SHA256.Create())
        {
            var buffer = new byte[1048576];
            int read;
            while ((read = input.Read(buffer, 0, buffer.Length)) > 0)
            { CheckPause(); hash.TransformBlock(buffer, 0, read, buffer, 0); Report(false); }
            hash.TransformFinalBlock(new byte[0], 0, 0);
            return BitConverter.ToString(hash.Hash).Replace("-", "").ToLowerInvariant() == part.Sha256;
        }
    }
    public static bool ValidateResume(int status, string contentRange, long contentLength, long offset, long size)
    {
        if (status == 206)
        {
            var match = Regex.Match(contentRange ?? "", @"^bytes (\d+)-(\d+)/(\d+)$");
            long start, end, length;
            if (!match.Success || !Int64.TryParse(match.Groups[1].Value, out start) || !Int64.TryParse(match.Groups[2].Value, out end) ||
                !Int64.TryParse(match.Groups[3].Value, out length) || start != offset || end != size - 1 || length != size ||
                (contentLength >= 0 && contentLength != size - offset)) throw new IOException("Invalid GitHub resume response. Partial data was preserved.");
            return true;
        }
        if (status == 200 && (contentLength == size || contentLength == -1)) return false;
        throw new IOException("Unexpected archive response. Partial data was preserved.");
    }
    void DownloadPart(Part part)
    {
        string full = Under(cache, part.Name), partial = Under(cache, part.Name + ".part"), meta = Under(cache, part.Name + ".meta");
        if (!Matches(cache, part))
        {
            if (File.Exists(full)) File.Delete(full);
            if (File.Exists(partial)) File.Delete(partial);
            AtomicText(meta, Identity(part));
        }
        if (File.Exists(full))
        {
            if (Verify(full, part)) return;
            File.Delete(full);
        }
        long offset = File.Exists(partial) ? new FileInfo(partial).Length : 0;
        if (offset > part.Size) { File.Delete(partial); offset = 0; }
        if (offset < part.Size)
        {
            CheckPause();
            using (var response = Request(part.Url, offset, false))
            {
                if ((response.ContentType ?? "").StartsWith("text/", StringComparison.OrdinalIgnoreCase)) throw new IOException("GitHub returned a page instead of an archive part.");
                if (!ValidateResume((int)response.StatusCode, response.Headers["Content-Range"], response.ContentLength, offset, part.Size)) offset = 0;
                if (Free(cache) < part.Size - offset + Reserve) throw new IOException("Not enough TEMP space for the next archive part.");
                long otherBytes = CachedBytes() - Math.Min(File.Exists(partial) ? new FileInfo(partial).Length : 0, part.Size);
                using (var input = response.GetResponseStream())
                using (var output = new FileStream(partial, offset == 0 ? FileMode.Create : FileMode.Open, FileAccess.Write, FileShare.Read))
                {
                    output.Position = offset;
                    long bytes = offset, sampleBytes = bytes;
                    double sampleTime = clock.Elapsed.TotalSeconds;
                    var buffer = new byte[262144];
                    phase = "downloading";
                    while (bytes < part.Size)
                    {
                        CheckPause();
                        int count = input.Read(buffer, 0, (int)Math.Min(buffer.Length, part.Size - bytes));
                        if (count == 0) throw new EndOfStreamException("Download interrupted. Resume to continue.");
                        output.Write(buffer, 0, count); bytes += count;
                        downloaded = otherBytes + bytes;
                        double now = clock.Elapsed.TotalSeconds;
                        if (now - sampleTime >= .2)
                        {
                            rate = (bytes - sampleBytes) / (now - sampleTime);
                            output.Flush(true); Report(true);
                            sampleBytes = bytes; sampleTime = now;
                        }
                    }
                    output.Flush(true);
                }
            }
        }
        if (!Verify(partial, part))
        {
            File.Delete(partial);
            throw new InvalidDataException("An archive part failed SHA-256. Resume to download it again.");
        }
        CheckPause();
        File.Move(partial, full);
        downloaded = CachedBytes();
    }
    void Relocate()
    {
        if (String.IsNullOrEmpty(previousCache) || !Directory.Exists(previousCache) || String.Equals(previousCache, cache, StringComparison.OrdinalIgnoreCase)) return;
        foreach (var part in parts)
        {
            CheckPause();
            if (!Matches(previousCache, part) || File.Exists(Under(cache, part.Name)) || File.Exists(Under(cache, part.Name + ".part"))) continue;
            string name = File.Exists(Under(previousCache, part.Name)) ? part.Name : part.Name + ".part";
            string source = Under(previousCache, name);
            if (!File.Exists(source)) continue;
            long size = new FileInfo(source).Length;
            if (size > part.Size) continue;
            string transfer = Under(cache, name + ".transfer"), marker = Under(cache, name + ".transfer.meta");
            string stamp = Identity(part) + "\n" + source + "\n" + size + "\n" + File.GetLastWriteTimeUtc(source).Ticks;
            long copied = File.Exists(marker) && File.ReadAllText(marker, Utf8) == stamp && File.Exists(transfer) ? new FileInfo(transfer).Length : 0;
            if (copied > size) copied = 0;
            if (Free(cache) < size - copied + Reserve) throw new IOException("Not enough space to move the cached archive parts.");
            AtomicText(marker, stamp);
            phase = "moving"; rate = 0; Report(true);
            using (var input = new FileStream(source, FileMode.Open, FileAccess.Read, FileShare.Read))
            using (var output = new FileStream(transfer, copied == 0 ? FileMode.Create : FileMode.Open, FileAccess.Write, FileShare.Read))
            {
                input.Position = output.Position = copied;
                var buffer = new byte[262144];
                while (copied < size)
                {
                    CheckPause();
                    int count = input.Read(buffer, 0, (int)Math.Min(buffer.Length, size - copied));
                    if (count == 0) throw new IOException("Cached part changed during relocation.");
                    output.Write(buffer, 0, count); copied += count; Report(false);
                }
                output.Flush(true);
            }
            CheckPause();
            AtomicText(Under(cache, part.Name + ".meta"), Identity(part));
            if (!MoveFileEx(transfer, Under(cache, name), 8)) throw new IOException("Cannot commit the relocated archive part.");
            File.Delete(source); File.Delete(marker);
        }
    }
    public static long SevenZipLength(byte[] header)
    {
        byte[] signature = { 0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c };
        if (header.Length < 32) throw new InvalidDataException("Truncated 7z header.");
        for (int i = 0; i < signature.Length; ++i) if (header[i] != signature[i]) throw new InvalidDataException("The first volume is not a 7z archive.");
        ulong offset = BitConverter.ToUInt64(header, 12), size = BitConverter.ToUInt64(header, 20);
        if (offset > Int64.MaxValue - 32UL || size > (ulong)Int64.MaxValue - 32UL - offset) throw new InvalidDataException("Invalid 7z archive length.");
        return (long)(32UL + offset + size);
    }

    static bool InFolder(string path, string folder)
    {
        return String.Equals(path, folder, StringComparison.OrdinalIgnoreCase) ||
            path.StartsWith(folder + "/", StringComparison.OrdinalIgnoreCase);
    }
    static bool Selected(string name)
    {
        name = name.TrimEnd('/');
        foreach (string folder in Folders) if (InFolder(name, folder)) return true;
        return InFolder(name, "game") || InFolder(name, "InGame") || InFolder(name, NestedInGame);
    }
    string InstallPath(string relative)
    {
        if (InFolder(relative, NestedInGame)) relative = relative.Substring("bins/".Length);
        return Under(InFolder(relative, "InGame") ? sdk : target, relative);
    }
    public static List<Entry> ParseListing(string text)
    {
        var records = new List<Dictionary<string, string>>();
        var record = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (string raw in (text + "\n\n").Replace("\r", "").Split('\n'))
        {
            if (raw.Length == 0)
            {
                if (record.ContainsKey("Path")) records.Add(record);
                record = new Dictionary<string, string>(StringComparer.Ordinal);
                continue;
            }
            int separator = raw.IndexOf(" = ", StringComparison.Ordinal);
            if (separator > 0) record[raw.Substring(0, separator)] = raw.Substring(separator + 3);
        }
        string prefix = null;
        foreach (var item in records)
        {
            if (item.ContainsKey("Type")) continue; // container metadata, not an entry
            string name = SafeName(item["Path"]);
            string candidate = Selected(name) ? "" : null;
            int slash = name.IndexOf('/');
            if (candidate == null && slash >= 0 && Selected(name.Substring(slash + 1))) candidate = name.Substring(0, slash + 1);
            if (candidate == null) continue;
            if (prefix != null && prefix != candidate) throw new InvalidDataException("Ambiguous SDK root in the archive.");
            prefix = candidate;
        }
        if (prefix == null) throw new InvalidDataException("The archive contains no SDK resource folders.");
        var result = new List<Entry>();
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        bool settings = false, gamedata = false, game = false;
        string inGame = null;
        foreach (var item in records)
        {
            if (item.ContainsKey("Type")) continue;
            string name = SafeName(item["Path"]);
            if (!name.StartsWith(prefix, StringComparison.Ordinal) || !Selected(name.Substring(prefix.Length))) continue;
            string relative = name.Substring(prefix.Length).TrimEnd('/');
            string attributes, value;
            item.TryGetValue("Attributes", out attributes);
            foreach (var property in item)
                if (property.Key.IndexOf("reparse", StringComparison.OrdinalIgnoreCase) >= 0 && property.Value.Length > 0)
                    throw new InvalidDataException("Reparse points are not allowed in resource archives.");
            if ((item.TryGetValue("Symbolic Link", out value) && value.Length > 0) ||
                (item.TryGetValue("Hard Link", out value) && value.Length > 0) ||
                (item.TryGetValue("Link", out value) && value.Length > 0) ||
                Regex.IsMatch(attributes ?? "", @"(?:^|\s)l[rwxstST-]{9}")) throw new InvalidDataException("Links are not allowed in resource archives.");
            if (item.TryGetValue("Encrypted", out value) && value == "+") throw new InvalidDataException("Encrypted resource archives are unsupported.");
            if ((item.TryGetValue("Anti", out value) && value == "+") || (item.TryGetValue("Alternate Stream", out value) && value == "+"))
                throw new InvalidDataException("Special archive entries are not allowed in resources.");
            bool directory = (item.TryGetValue("Folder", out value) && value == "+") || (attributes ?? "").StartsWith("D", StringComparison.Ordinal);
            long size = 0;
            if (!directory && (!item.TryGetValue("Size", out value) || !Int64.TryParse(value, out size) || size < 0)) throw new InvalidDataException("Invalid archive entry size.");
            if (!names.Add(relative)) throw new InvalidDataException("Duplicate resource path in the archive.");
            if (InFolder(relative, "settings")) settings = true;
            if (InFolder(relative, "gamedata")) gamedata = true;
            if (InFolder(relative, "game")) game = true;
            string runtime = InFolder(relative, NestedInGame) ? NestedInGame : InFolder(relative, "InGame") ? "InGame" : null;
            if (runtime != null)
            {
                if (inGame != null && inGame != runtime) throw new InvalidDataException("Ambiguous InGame folder in the archive.");
                inGame = runtime;
            }
            result.Add(new Entry { Name = name.TrimEnd('/'), Relative = relative, Size = size, Directory = directory });
        }
        if (!settings || !gamedata || !game || inGame == null)
            throw new InvalidDataException("The archive must contain settings, gamedata, game and InGame.");
        return result;
    }
    static string Quote(string value)
    {
        var result = new StringBuilder("\"");
        int slashes = 0;
        foreach (char c in value)
        {
            if (c == '\\') { ++slashes; continue; }
            result.Append('\\', c == '"' ? slashes * 2 + 1 : slashes); slashes = 0; result.Append(c);
        }
        result.Append('\\', slashes * 2); return result.Append('"').ToString();
    }
    string SevenZip(string arguments, bool listing)
    {
        IntPtr job = CreateJobObject(IntPtr.Zero, null);
        if (job == IntPtr.Zero) throw new IOException("Cannot create the archive worker job.");
        var limits = new ExtendedLimits { Basic = new BasicLimits { Flags = 0x2000 } }; // kill children on job close
        int length = Marshal.SizeOf(limits);
        IntPtr memory = Marshal.AllocHGlobal(length);
        try
        {
            Marshal.StructureToPtr(limits, memory, false);
            if (!SetInformationJobObject(job, 9, memory, (uint)length)) throw new IOException("Cannot configure the archive worker job.");
            var start = new ProcessStartInfo(Path.Combine(work, "7z.exe"), arguments) {
                UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = work,
                RedirectStandardOutput = true, RedirectStandardError = true, RedirectStandardInput = true,
                StandardOutputEncoding = new UTF8Encoding(false), StandardErrorEncoding = new UTF8Encoding(false) };
            using (var process = Process.Start(start))
            {
                bool assigned = false;
                try
                {
                    if (!AssignProcessToJobObject(job, process.Handle)) throw new IOException("Cannot protect the archive worker process.");
                    assigned = true;
                    process.StandardInput.Close();
                    var output = new StringBuilder();
                    var errors = new StringBuilder();
                    int percentage = 0;
                    Task stdout = Task.Factory.StartNew(() => {
                        var chars = new char[1024]; string tail = ""; int count;
                        while ((count = process.StandardOutput.Read(chars, 0, chars.Length)) > 0)
                        {
                            if (listing)
                            {
                                if (output.Length + count > 67108864) throw new IOException("Archive listing is too large.");
                                output.Append(chars, 0, count);
                            }
                            else
                            {
                                tail += new String(chars, 0, count);
                                foreach (Match match in Regex.Matches(tail, @"(\d{1,3})%")) Interlocked.Exchange(ref percentage, Math.Min(100, Int32.Parse(match.Groups[1].Value, CultureInfo.InvariantCulture)));
                                if (tail.Length > 64) tail = tail.Substring(tail.Length - 64);
                            }
                        }
                    });
                    Task stderr = Task.Factory.StartNew(() => {
                        var chars = new char[1024]; int count;
                        while ((count = process.StandardError.Read(chars, 0, chars.Length)) > 0)
                        { errors.Append(chars, 0, count); if (errors.Length > 16384) errors.Remove(0, errors.Length - 16384); }
                    });
                    while (!process.WaitForExit(100))
                    {
                        CheckPause();
                        if (stdout.IsFaulted || stderr.IsFaulted) throw new IOException("Cannot read archive worker output.");
                        if (!listing) extracted = extractTotal * Interlocked.CompareExchange(ref percentage, 0, 0) / 100;
                        Report(false);
                    }
                    Task.WaitAll(stdout, stderr);
                    CheckPause();
                    if (process.ExitCode != 0)
                    {
                        AtomicText(Path.Combine(work, "7zip-error.txt"), errors.ToString());
                        throw new InvalidDataException("7-Zip could not read or verify the archive. See 7zip-error.txt.");
                    }
                    return output.ToString();
                }
                finally
                {
                    if (!process.HasExited)
                    {
                        if (assigned) { CloseHandle(job); job = IntPtr.Zero; }
                        else { try { process.Kill(); } catch (InvalidOperationException) { } }
                        process.WaitForExit(2000);
                    }
                }
            }
        }
        finally { Marshal.FreeHGlobal(memory); if (job != IntPtr.Zero) CloseHandle(job); }
    }
    void Extract()
    {
        phase = "extracting"; rate = 0; Report(true);
        string first = Under(cache, parts[0].Name);
        var entries = ParseListing(SevenZip("l -slt -ba -sccUTF-8 -bsp0 -- " + Quote(first), true));
        extractTotal = 0;
        var includes = new StringBuilder();
        foreach (var entry in entries)
        {
            if (entry.Directory || File.Exists(InstallPath(entry.Relative))) continue;
            extractTotal = checked(extractTotal + entry.Size);
            includes.Append(entry.Name.Replace('/', '\\')).Append('\n');
        }
        // Solid 7z streams restart extraction after a pause. Never trust a partial
        // staging file; only a zero-exit, CRC-checked extraction may be committed.
        if (Directory.Exists(stage)) DeleteStage(stage);
        Space(0, extractTotal);
        Directory.CreateDirectory(stage);
        string includeFile = Path.Combine(work, "resource-files.txt");
        AtomicText(includeFile, includes.ToString());
        if (includes.Length > 0)
            SevenZip("x -y -aoa -spd -sccUTF-8 -scsUTF-8 -bso0 -bse2 -bsp1 " + Quote("-o" + stage) +
                " " + Quote("-i@" + includeFile) + " -- " + Quote(first), false);
        string prefix = entries[0].Name.Substring(0, entries[0].Name.Length - entries[0].Relative.Length);
        string root = prefix.Length == 0 ? stage : Under(stage, prefix.TrimEnd('/'));
        Directory.CreateDirectory(root);
        foreach (var entry in entries)
        {
            string path = Under(root, entry.Relative);
            if (entry.Directory) { Directory.CreateDirectory(path); continue; }
            if (File.Exists(InstallPath(entry.Relative))) continue;
            if (!File.Exists(path) || new FileInfo(path).Length != entry.Size)
                throw new InvalidDataException("Extracted resources do not match the archive listing.");
        }
        extracted = extractTotal; Report(true);
        Install(root);
    }
    void Merge(string source, string destination)
    {
        CheckPause(); NoLinks(stage, source); NoLinks(target, destination);
        if (!Directory.Exists(destination)) { Directory.Move(source, destination); return; }
        foreach (string file in Directory.GetFiles(source))
        {
            CheckPause(); NoLinks(stage, file);
            string output = Under(destination, Path.GetFileName(file));
            if (!File.Exists(output)) File.Move(file, output);
        }
        foreach (string directory in Directory.GetDirectories(source)) Merge(directory, Path.Combine(destination, Path.GetFileName(directory)));
    }
    void DeleteStage(string directory)
    {
        CheckPause(); NoLinks(stage, directory);
        foreach (string file in Directory.GetFiles(directory))
        { NoLinks(stage, file); File.SetAttributes(file, File.GetAttributes(file) & ~FileAttributes.ReadOnly); File.Delete(file); }
        foreach (string child in Directory.GetDirectories(directory)) DeleteStage(child);
        File.SetAttributes(directory, File.GetAttributes(directory) & ~FileAttributes.ReadOnly);
        Directory.Delete(directory);
    }
    void Install(string root)
    {
        phase = "installing"; Report(true);
        string game = Under(root, "game");
        if (Directory.Exists(game)) Merge(game, InstallPath("game"));
        foreach (string folder in new[] { "InGame", NestedInGame })
        {
            string runtime = Under(root, folder);
            if (Directory.Exists(runtime)) Merge(runtime, InstallPath(folder));
        }
        string last = !Directory.Exists(Under(target, "gamedata")) ? "gamedata" : "settings";
        foreach (string folder in Folders)
            if (folder != last && Directory.Exists(Under(root, folder))) Merge(Under(root, folder), Under(target, folder));
        if (Directory.Exists(Under(root, last))) Merge(Under(root, last), Under(target, last));
        if (!Directory.Exists(Under(target, "settings")) || !Directory.Exists(Under(target, "gamedata")) ||
            !Directory.Exists(InstallPath("game")) || !Directory.Exists(InstallPath("InGame")))
            throw new IOException("Required resource folders were not installed.");
        phase = "complete"; Report(true);
        try
        {
            DeleteStage(stage);
            foreach (var part in parts) { File.Delete(Under(cache, part.Name)); File.Delete(Under(cache, part.Name + ".meta")); }
            File.Delete(Path.Combine(work, "manifest.json"));
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }
    public static int Run(string target, string sdk, string work, string archiveDirectory, string previousArchiveDirectory, int parentId, long parentStart)
    {
        ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
        var worker = new SDKResourceWorker { target = Root(target), sdk = Root(sdk), work = Root(work), cache = Root(archiveDirectory),
            previousCache = String.IsNullOrEmpty(previousArchiveDirectory) ? "" : Root(previousArchiveDirectory), parentId = parentId, parentStart = parentStart };
        worker.stage = Path.Combine(worker.target, ".sdk-resource-stage-github-v1");
        FileStream installationLock;
        try { installationLock = new FileStream(Path.Combine(worker.work, "worker.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None); }
        catch (IOException) { return 2; }
        using (installationLock)
        {
            try
            {
                NoLinks(worker.work, worker.work); NoLinks(worker.target, worker.stage); NoLinks(worker.target, worker.sdk);
                worker.CheckPause(); Directory.CreateDirectory(worker.cache); NoLinks(worker.cache, worker.cache);
                worker.Report(true);
                worker.parts = worker.LoadManifest();
                worker.total = 0; foreach (var part in worker.parts) worker.total = checked(worker.total + part.Size);
                worker.Relocate(); worker.downloaded = worker.CachedBytes(); worker.Report(true);
                if (Directory.Exists(worker.stage)) worker.DeleteStage(worker.stage);
                worker.Space(worker.total - worker.downloaded, ResourceEstimate - worker.ExistingResourceBytes());
                foreach (var part in worker.parts)
                {
                    worker.DownloadPart(part);
                    if (part.Number == 1)
                    {
                        byte[] header = new byte[32];
                        using (var input = File.OpenRead(Under(worker.cache, part.Name))) if (input.Read(header, 0, 32) != 32) throw new InvalidDataException("Truncated first 7z volume.");
                        if (SevenZipLength(header) != worker.total) throw new InvalidDataException("The split archive is incomplete or contains mixed volumes.");
                    }
                }
                worker.Extract();
                return 0;
            }
            catch (Exception error)
            {
                worker.rate = 0;
                if (error is Paused || File.Exists(Path.Combine(worker.work, "pause"))) worker.phase = "paused";
                else
                {
                    worker.phase = "error"; worker.message = error.Message;
                    try { File.WriteAllText(Path.Combine(worker.work, "worker-error.txt"), error.ToString(), Utf8); } catch (IOException) { }
                }
                try { worker.Report(true); } catch (IOException) { }
                return worker.phase == "paused" ? 0 : 1;
            }
        }
    }
}
