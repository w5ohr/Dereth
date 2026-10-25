# Homebrew cask for the Open WebUI desktop client. scripts/make-dmg.sh writes a
# copy with the real version and sha256 next to the DMG it builds; publish the
# DMG as a GitHub release asset under the tag below, then put that copy in a tap
# (e.g. w5ohr/homebrew-tap/Casks/open-webui-desktop.rb).
cask "open-webui-desktop" do
  version "@VERSION@"
  sha256 "@SHA256@"

  url "https://github.com/w5ohr/Dereth/releases/download/owui-desktop-v#{version}/Open-WebUI-#{version}.dmg"
  name "Open WebUI Desktop"
  desc "Native macOS client for Open WebUI and local models"
  homepage "https://github.com/w5ohr/Dereth/tree/main/owui-desktop"

  depends_on macos: ">= :ventura"

  app "Open WebUI.app"

  zap trash: [
    "~/Library/Application Support/Open WebUI Desktop",
    "~/Library/Preferences/com.davidcochran.owui-desktop.plist",
    "~/Library/Saved Application State/com.davidcochran.owui-desktop.savedState",
  ]

  caveats <<~EOS
    This build is ad-hoc signed, not notarised (Developer ID signing is a
    separate step). The first launch needs a right-click > Open, or:
      xattr -dr com.apple.quarantine "/Applications/Open WebUI.app"
  EOS
end
