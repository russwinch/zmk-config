import Foundation

/// One thing to flash: a half or the dongle.
public struct Device: Codable, Hashable, Identifiable, Sendable {
    public let name: String  // "dongle", "left" or "right"
    public let what: String  // shown to the person
    public let firmware: String  // first shield in build.yaml, or the entry's artifact-name
    public let board: String  // key into Targets.boards

    public var id: String { name }
}

public struct Keyboard: Codable, Hashable, Identifiable, Sendable {
    public let id: String
    public let label: String
    /// In flashing (and so pairing) order.
    public let devices: [Device]
    /// Shown while flashing with a settings reset.
    public let resetNote: String?

    enum CodingKeys: String, CodingKey {
        case id, label, devices
        case resetNote = "reset_note"
    }
}

public struct Board: Codable, Hashable, Sendable {
    public let label: String
    /// Words looked for in a bootloader drive's INFO_UF2.TXT.
    public let hints: [String]
}

/// tools/targets.json, shared with tools/flash.py.
public struct Targets: Codable, Sendable {
    public let repo: String
    public let boards: [String: Board]
    public let keyboards: [Keyboard]

    public static func load(from url: URL) throws -> Targets {
        try JSONDecoder().decode(Targets.self, from: Data(contentsOf: url))
    }
}
