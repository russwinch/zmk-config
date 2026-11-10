import Foundation

public enum FlashError: LocalizedError, Equatable {
    case firmwareNotFound(firmware: String, board: String, found: [String])
    case firmwareAmbiguous(firmware: String, board: String, found: [String])
    case notFirmware(String)
    case nothingToFlash
    case stillMounted(String)
    case tool(String)

    public var errorDescription: String? {
        switch self {
        case let .firmwareNotFound(firmware, board, found):
            let list = found.isEmpty ? "It has no .uf2 files at all." : "It has: " + found.joined(separator: ", ")
            return "No firmware for \(firmware) (\(board)) in there. \(list)"
        case let .firmwareAmbiguous(firmware, board, found):
            return "More than one file matches \(firmware) (\(board)): " + found.joined(separator: ", ")
        case let .notFirmware(name):
            return "\(name) is not a .zip or a folder of .uf2 files."
        case .nothingToFlash:
            return "No devices selected."
        case let .stillMounted(name):
            return "\(name) is still there 30 seconds after the copy, so the flash probably didn't take."
        case let .tool(message):
            return message
        }
    }
}

public enum StepKind: String, Sendable {
    case settingsReset
    case firmware
}

/// One trip into the bootloader: one file onto one device.
public struct Step: Identifiable, Hashable, Sendable {
    public let id: Int
    public let device: Device
    public let kind: StepKind
    public let uf2: URL

    public init(id: Int, device: Device, kind: StepKind, uf2: URL) {
        self.id = id
        self.device = device
        self.kind = kind
        self.uf2 = uf2
    }
}

public enum Firmware {
    public static let settingsReset = "settings_reset"

    /// (first shield or artifact-name, board) for a CI-built .uf2.
    ///
    /// ZMK's workflow names files "<shield list>-<board with / as _>-zmk.uf2",
    /// e.g. "waterfowl_left nice_view_adapter nice_view-nice_nano__zmk-zmk.uf2",
    /// or "<artifact-name>.uf2" when build.yaml sets one.
    public static func key(for file: URL) -> (firmware: String, board: String?) {
        let stem = file.deletingPathExtension().lastPathComponent
        let parts = stem.components(separatedBy: "-")
        guard parts.count >= 3, parts.last == "zmk" else { return (stem, nil) }
        let shields = parts.dropLast(2).joined(separator: "-")
        let board = parts[parts.count - 2].components(separatedBy: "__")[0]
        return (shields.split(separator: " ").first.map(String.init) ?? shields, board)
    }

    public static func find(_ files: [URL], firmware: String, board: String) throws -> URL {
        let matches = files.filter {
            let key = key(for: $0)
            return key.firmware == firmware && (key.board == nil || key.board == board)
        }
        let names = files.map(\.lastPathComponent).sorted()
        switch matches.count {
        case 1: return matches[0]
        case 0: throw FlashError.firmwareNotFound(firmware: firmware, board: board, found: names)
        default:
            throw FlashError.firmwareAmbiguous(
                firmware: firmware, board: board, found: matches.map(\.lastPathComponent).sorted())
        }
    }

    /// The .uf2 files in a folder, or in a CI firmware .zip (unpacked to a temporary folder).
    public static func unpack(_ source: URL) throws -> [URL] {
        var isDirectory: ObjCBool = false
        guard FileManager.default.fileExists(atPath: source.path, isDirectory: &isDirectory) else {
            throw FlashError.tool("\(source.lastPathComponent) is no longer there.")
        }
        var folder = source
        if !isDirectory.boolValue {
            guard source.pathExtension.lowercased() == "zip" else {
                throw FlashError.notFirmware(source.lastPathComponent)
            }
            folder = try temporaryFolder()
            let unzip = Process()
            unzip.executableURL = URL(fileURLWithPath: "/usr/bin/unzip")
            unzip.arguments = ["-q", "-o", source.path, "-d", folder.path]
            try unzip.run()
            unzip.waitUntilExit()
            guard unzip.terminationStatus == 0 else {
                throw FlashError.tool("Couldn't unzip \(source.lastPathComponent).")
            }
        }
        var found: [URL] = []
        let walker = FileManager.default.enumerator(at: folder, includingPropertiesForKeys: nil)
        while let file = walker?.nextObject() as? URL {
            if file.pathExtension.lowercased() == "uf2" { found.append(file) }
        }
        return found.sorted { $0.path < $1.path }
    }

    /// Newest firmware*.zip in ~/Downloads (where the browser puts the CI artifact).
    public static func newestDownload(in folder: URL? = nil) -> URL? {
        let folder = folder ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Downloads")
        let files = (try? FileManager.default.contentsOfDirectory(
            at: folder, includingPropertiesForKeys: [.contentModificationDateKey])) ?? []
        return files
            .filter { $0.lastPathComponent.hasPrefix("firmware") && $0.pathExtension.lowercased() == "zip" }
            .max { modified($0) < modified($1) }
    }

    public static func modified(_ file: URL) -> Date {
        (try? file.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast
    }

    /// The flashes to do, in order. With `reset`, each device gets
    /// settings_reset first and then its own firmware.
    public static func plan(keyboard: Keyboard, only: Set<String>? = nil, reset: Bool, files: [URL]) throws -> [Step] {
        var steps: [Step] = []
        for device in keyboard.devices where only?.contains(device.name) ?? true {
            if reset {
                let uf2 = try find(files, firmware: settingsReset, board: device.board)
                steps.append(Step(id: steps.count, device: device, kind: .settingsReset, uf2: uf2))
            }
            let uf2 = try find(files, firmware: device.firmware, board: device.board)
            steps.append(Step(id: steps.count, device: device, kind: .firmware, uf2: uf2))
        }
        if steps.isEmpty { throw FlashError.nothingToFlash }
        return steps
    }

    static func temporaryFolder() throws -> URL {
        let folder = FileManager.default.temporaryDirectory
            .appendingPathComponent("zmk-firmware-" + UUID().uuidString)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        return folder
    }
}
