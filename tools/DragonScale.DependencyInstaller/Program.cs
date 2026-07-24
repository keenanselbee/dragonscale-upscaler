using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;
using System.Xml.Linq;

namespace DragonScale.DependencyInstaller
{
    internal static class Program
    {
        private const string ManifestName = "DragonScale.DependencyInstaller.manifest.xml";
        private const string DependencyRelativePath = @"SKSE\Plugins\DragonScale";
        private const string LockRelativePath = DependencyRelativePath + @"\dependencies.lock.json";

        private static int Main(string[] args)
        {
            try {
                ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;

                var modRoot = AppDomain.CurrentDomain.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
                var manifestPath = Path.Combine(modRoot, ManifestName);
                if (!File.Exists(manifestPath)) {
                    Fail("Missing manifest: " + manifestPath);
                    return 2;
                }

                var manifest = Manifest.Load(manifestPath);
                var requested = SelectPackages(manifest.Packages, args);
                if (requested.Count == 0) {
                    WriteHeader(modRoot);
                    Console.WriteLine("No packages selected.");
                    WaitForExit(args);
                    return 0;
                }

                WriteHeader(modRoot);
                if (args.Any(a => EqualsArg(a, "--dry-run"))) {
                    foreach (var package in requested) {
                        var release = GitHubReleaseClient.GetRelease(package);
                        var asset = release.Assets.FirstOrDefault(a => Regex.IsMatch(a.Name, package.AssetRegex, RegexOptions.IgnoreCase));

                        Console.WriteLine("Package: " + package.Name);
                        Console.WriteLine("Release: " + release.TagName);
                        Console.WriteLine("Asset: " + (asset == null ? "(source zip fallback)" : asset.Name));
                        Console.WriteLine();
                    }

                    Console.WriteLine("Dry run complete. No files were downloaded.");
                    WaitForExit(args);
                    return 0;
                }

                Console.WriteLine("Installing to:");
                Console.WriteLine("  " + Path.Combine(modRoot, DependencyRelativePath));
                Console.WriteLine();

                var lockEntries = new List<LockEntry>();
                foreach (var package in requested) {
                    var installer = new PackageInstaller(modRoot, package);
                    lockEntries.AddRange(installer.Install());
                }

                WriteLockFile(modRoot, lockEntries);
                Console.WriteLine();
                Console.WriteLine("Done.");
                WaitForExit(args);
                return 0;
            } catch (Exception ex) {
                Fail(ex.Message);
                WaitForExit(args);
                return 1;
            }
        }

        private static void WriteHeader(string modRoot)
        {
            Console.WriteLine("DragonScale Dependency Installer");
            Console.WriteLine("--------------------------------");
            Console.WriteLine("Mod root: " + modRoot);
            Console.WriteLine();
        }

        private static List<PackageDefinition> SelectPackages(IEnumerable<PackageDefinition> packages, string[] args)
        {
            var list = packages.ToList();
            var packageArgs = args.Where(a => !EqualsArg(a, "--dry-run")).ToList();
            if (packageArgs.Count == 0 || packageArgs.Any(a => EqualsArg(a, "--all"))) {
                return list;
            }
            if (args.Any(a => EqualsArg(a, "--nvidia"))) {
                return list.Where(p => p.Vendor.Equals("NVIDIA", StringComparison.OrdinalIgnoreCase)).ToList();
            }
            return new List<PackageDefinition>();
        }

        private static bool EqualsArg(string actual, string expected)
        {
            return actual.Equals(expected, StringComparison.OrdinalIgnoreCase);
        }

        private static void WriteLockFile(string modRoot, IEnumerable<LockEntry> entries)
        {
            var lockPath = Path.Combine(modRoot, LockRelativePath);
            Directory.CreateDirectory(Path.GetDirectoryName(lockPath));

            var sb = new StringBuilder();
            sb.AppendLine("{");
            sb.AppendLine("  \"generatedAtUtc\": \"" + JsonEscape(DateTime.UtcNow.ToString("O")) + "\",");
            sb.AppendLine("  \"entries\": [");

            var materialized = entries.ToList();
            for (var i = 0; i < materialized.Count; i++) {
                var entry = materialized[i];
                sb.AppendLine("    {");
                sb.AppendLine("      \"packageId\": \"" + JsonEscape(entry.PackageId) + "\",");
                sb.AppendLine("      \"packageName\": \"" + JsonEscape(entry.PackageName) + "\",");
                sb.AppendLine("      \"vendor\": \"" + JsonEscape(entry.Vendor) + "\",");
                sb.AppendLine("      \"releaseTag\": \"" + JsonEscape(entry.ReleaseTag) + "\",");
                sb.AppendLine("      \"sourceUrl\": \"" + JsonEscape(entry.SourceUrl) + "\",");
                sb.AppendLine("      \"relativePath\": \"" + JsonEscape(entry.RelativePath.Replace('\\', '/')) + "\",");
                sb.AppendLine("      \"sha256\": \"" + JsonEscape(entry.Sha256) + "\",");
                sb.AppendLine("      \"signer\": \"" + JsonEscape(entry.Signer) + "\"");
                sb.Append("    }");
                if (i < materialized.Count - 1) {
                    sb.Append(",");
                }
                sb.AppendLine();
            }

            sb.AppendLine("  ]");
            sb.AppendLine("}");

            File.WriteAllText(lockPath, sb.ToString(), new UTF8Encoding(false));
            Console.WriteLine("Wrote lock file:");
            Console.WriteLine("  " + lockPath);
        }

        private static string JsonEscape(string value)
        {
            if (value == null) {
                return string.Empty;
            }

            return value.Replace("\\", "\\\\").Replace("\"", "\\\"").Replace("\r", "\\r").Replace("\n", "\\n");
        }

        private static void Fail(string message)
        {
            Console.Error.WriteLine();
            Console.Error.WriteLine("Error: " + message);
        }

        private static void WaitForExit(string[] args)
        {
            if (args.Length > 0) {
                return;
            }

            Console.WriteLine();
            Console.Write("Press Enter to close...");
            Console.ReadLine();
        }
    }

    internal sealed class PackageInstaller
    {
        private readonly string modRoot;
        private readonly PackageDefinition package;

        public PackageInstaller(string modRoot, PackageDefinition package)
        {
            this.modRoot = modRoot;
            this.package = package;
        }

        public List<LockEntry> Install()
        {
            Console.WriteLine("Package: " + package.Name);
            if (!string.IsNullOrWhiteSpace(package.LicenseUrl)) {
                Console.WriteLine("License: " + package.LicenseUrl);
            }

            var release = GitHubReleaseClient.GetRelease(package);
            var asset = release.Assets.FirstOrDefault(a => Regex.IsMatch(a.Name, package.AssetRegex, RegexOptions.IgnoreCase));
            var downloadUrl = asset != null ? asset.DownloadUrl : release.ZipballUrl;
            var sourceName = asset != null ? asset.Name : release.TagName + "-source.zip";

            if (string.IsNullOrWhiteSpace(downloadUrl)) {
                throw new InvalidOperationException("Could not find a download URL for " + package.Name);
            }

            Console.WriteLine("Release: " + release.TagName);
            Console.WriteLine("Download: " + sourceName);

            var tempRoot = Path.Combine(Path.GetTempPath(), "DragonScale.DependencyInstaller", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(tempRoot);

            try {
                var archivePath = Path.Combine(tempRoot, sourceName.EndsWith(".zip", StringComparison.OrdinalIgnoreCase) ? sourceName : "package.zip");
                DownloadFile(downloadUrl, archivePath);

                var extractRoot = Path.Combine(tempRoot, "extract");
                Directory.CreateDirectory(extractRoot);

                if (LooksLikeZip(archivePath)) {
                    ZipFile.ExtractToDirectory(archivePath, extractRoot);
                } else {
                    File.Copy(archivePath, Path.Combine(extractRoot, sourceName), true);
                }

                var entries = new List<LockEntry>();
                foreach (var file in package.Files) {
                    var source = FindFile(extractRoot, file.SearchRegex);
                    if (source == null) {
                        if (file.Required) {
                            throw new FileNotFoundException("Could not find " + file.OutputFile + " in " + sourceName);
                        }

                        Console.WriteLine("Skipped optional file: " + file.OutputFile);
                        continue;
                    }

                    var dependencyDir = Path.Combine(modRoot, @"SKSE\Plugins\DragonScale");
                    Directory.CreateDirectory(dependencyDir);

                    var destination = Path.Combine(dependencyDir, file.OutputFile);
                    File.Copy(source, destination, true);

                    var signer = GetSigner(destination);
                    var sha256 = GetSha256(destination);
                    var relativePath = MakeRelative(modRoot, destination);

                    Console.WriteLine("Installed:");
                    Console.WriteLine("  " + relativePath);
                    Console.WriteLine("  SHA256: " + sha256);
                    if (!string.IsNullOrWhiteSpace(signer)) {
                        Console.WriteLine("  Signer: " + signer);
                    }

                    entries.Add(new LockEntry {
                        PackageId = package.Id,
                        PackageName = package.Name,
                        Vendor = package.Vendor,
                        ReleaseTag = release.TagName,
                        SourceUrl = downloadUrl,
                        RelativePath = relativePath,
                        Sha256 = sha256,
                        Signer = signer
                    });
                }

                Console.WriteLine();
                return entries;
            } finally {
                try {
                    Directory.Delete(tempRoot, true);
                } catch {
                }
            }
        }

        private static void DownloadFile(string url, string destination)
        {
            using (var client = new WebClient()) {
                client.Headers[HttpRequestHeader.UserAgent] = "DragonScale.DependencyInstaller/1.0";
                client.Headers[HttpRequestHeader.Accept] = "application/octet-stream";
                client.DownloadFile(url, destination);
            }
        }

        private static bool LooksLikeZip(string path)
        {
            try {
                using (ZipFile.OpenRead(path)) {
                    return true;
                }
            } catch {
                return false;
            }
        }

        private static string FindFile(string root, string searchRegex)
        {
            var regex = new Regex(searchRegex, RegexOptions.IgnoreCase);
            return Directory
                .EnumerateFiles(root, "*", SearchOption.AllDirectories)
                .Select(path => new {
                    Path = path,
                    Relative = MakeRelative(root, path).Replace('\\', '/')
                })
                .Where(item => regex.IsMatch(item.Relative))
                .OrderBy(item => item.Relative.Length)
                .Select(item => item.Path)
                .FirstOrDefault();
        }

        private static string GetSha256(string path)
        {
            using (var stream = File.OpenRead(path))
            using (var sha = SHA256.Create()) {
                return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", string.Empty).ToLowerInvariant();
            }
        }

        private static string GetSigner(string path)
        {
            try {
                var cert = X509Certificate.CreateFromSignedFile(path);
                return cert.Subject;
            } catch {
                return string.Empty;
            }
        }

        private static string MakeRelative(string root, string path)
        {
            var rootUri = new Uri(AppendDirectorySeparator(root));
            var pathUri = new Uri(path);
            return Uri.UnescapeDataString(rootUri.MakeRelativeUri(pathUri).ToString()).Replace('/', Path.DirectorySeparatorChar);
        }

        private static string AppendDirectorySeparator(string path)
        {
            if (path.EndsWith(Path.DirectorySeparatorChar.ToString(), StringComparison.Ordinal) ||
                path.EndsWith(Path.AltDirectorySeparatorChar.ToString(), StringComparison.Ordinal)) {
                return path;
            }

            return path + Path.DirectorySeparatorChar;
        }
    }

    internal static class GitHubReleaseClient
    {
        public static ReleaseInfo GetRelease(PackageDefinition package)
        {
            var releasePart = package.Release.Equals("latest", StringComparison.OrdinalIgnoreCase)
                ? "latest"
                : "tags/" + package.Release;
            var url = "https://api.github.com/repos/" + package.Owner + "/" + package.Repo + "/releases/" + releasePart;

            string json;
            using (var client = new WebClient()) {
                client.Headers[HttpRequestHeader.UserAgent] = "DragonScale.DependencyInstaller/1.0";
                client.Headers[HttpRequestHeader.Accept] = "application/vnd.github+json";
                json = client.DownloadString(url);
            }

            var serializer = new JavaScriptSerializer();
            var root = serializer.Deserialize<Dictionary<string, object>>(json);

            var release = new ReleaseInfo {
                TagName = ReadString(root, "tag_name", package.Release),
                ZipballUrl = ReadString(root, "zipball_url", string.Empty)
            };

            object assetsObject;
            if (root.TryGetValue("assets", out assetsObject)) {
                var assets = assetsObject as IEnumerable;
                if (assets != null) {
                    foreach (var assetObject in assets) {
                        var asset = assetObject as Dictionary<string, object>;
                        if (asset == null) {
                            continue;
                        }

                        release.Assets.Add(new AssetInfo {
                            Name = ReadString(asset, "name", string.Empty),
                            DownloadUrl = ReadString(asset, "browser_download_url", string.Empty)
                        });
                    }
                }
            }

            return release;
        }

        private static string ReadString(Dictionary<string, object> map, string key, string fallback)
        {
            object value;
            if (!map.TryGetValue(key, out value) || value == null) {
                return fallback;
            }

            return Convert.ToString(value);
        }
    }

    internal sealed class Manifest
    {
        public List<PackageDefinition> Packages { get; private set; }

        public static Manifest Load(string path)
        {
            var doc = XDocument.Load(path);
            var packages = doc.Root
                .Elements("package")
                .Select(PackageDefinition.FromXml)
                .ToList();

            return new Manifest {
                Packages = packages
            };
        }
    }

    internal sealed class PackageDefinition
    {
        public string Id { get; private set; }
        public string Name { get; private set; }
        public string Vendor { get; private set; }
        public string Owner { get; private set; }
        public string Repo { get; private set; }
        public string Release { get; private set; }
        public string AssetRegex { get; private set; }
        public string LicenseUrl { get; private set; }
        public bool EnabledByDefault { get; private set; }
        public List<FileDefinition> Files { get; private set; }

        public static PackageDefinition FromXml(XElement element)
        {
            return new PackageDefinition {
                Id = Attr(element, "id"),
                Name = Attr(element, "name"),
                Vendor = Attr(element, "vendor"),
                Owner = Attr(element, "owner"),
                Repo = Attr(element, "repo"),
                Release = Attr(element, "release", "latest"),
                AssetRegex = Attr(element, "assetRegex", ".*\\.zip$"),
                LicenseUrl = Attr(element, "licenseUrl", string.Empty),
                EnabledByDefault = Attr(element, "enabledByDefault", "true").Equals("true", StringComparison.OrdinalIgnoreCase),
                Files = element.Elements("file").Select(FileDefinition.FromXml).ToList()
            };
        }

        private static string Attr(XElement element, string name, string fallback = "")
        {
            var attribute = element.Attribute(name);
            return attribute == null ? fallback : attribute.Value;
        }
    }

    internal sealed class FileDefinition
    {
        public string SearchRegex { get; private set; }
        public string OutputFile { get; private set; }
        public bool Required { get; private set; }

        public static FileDefinition FromXml(XElement element)
        {
            return new FileDefinition {
                SearchRegex = Attr(element, "searchRegex"),
                OutputFile = Attr(element, "outputFile"),
                Required = !Attr(element, "required", "true").Equals("false", StringComparison.OrdinalIgnoreCase)
            };
        }

        private static string Attr(XElement element, string name, string fallback = "")
        {
            var attribute = element.Attribute(name);
            return attribute == null ? fallback : attribute.Value;
        }
    }

    internal sealed class ReleaseInfo
    {
        public string TagName { get; set; }
        public string ZipballUrl { get; set; }
        public List<AssetInfo> Assets { get; private set; }

        public ReleaseInfo()
        {
            Assets = new List<AssetInfo>();
        }
    }

    internal sealed class AssetInfo
    {
        public string Name { get; set; }
        public string DownloadUrl { get; set; }
    }

    internal sealed class LockEntry
    {
        public string PackageId { get; set; }
        public string PackageName { get; set; }
        public string Vendor { get; set; }
        public string ReleaseTag { get; set; }
        public string SourceUrl { get; set; }
        public string RelativePath { get; set; }
        public string Sha256 { get; set; }
        public string Signer { get; set; }
    }
}
