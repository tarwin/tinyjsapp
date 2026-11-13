class Tinyjs < Formula
  desc "Build tiny native apps with JavaScript and the system webview"
  homepage "https://tinyjs.app"
  license "MIT"

  depends_on :macos

  on_macos do
    # The launcher targets macOS 14, but the bundled txiki.js targets macOS 15.
    depends_on macos: :sequoia

    on_arm do
      url "https://github.com/tarwin/tinyjsapp/releases/download/v0.56.0/tinyjs-macos-arm64.tar.gz"
      sha256 "a61e3d57f3b5e3d623bfb4e8023b8610459f7d9990717f11f0462ba025cc0667"
    end
    on_intel do
      url "https://github.com/tarwin/tinyjsapp/releases/download/v0.56.0/tinyjs-macos-x86_64.tar.gz"
      sha256 "4df85a397ad3d0f2d8e504471e1376623c94ac27983d91f84b228c24e9fc4cba"
    end
  end

  def install
    libexec.install Dir["*"]
    # Keep the release layout intact and avoid the upstream wrapper's
    # single-readlink assumption when Homebrew adds another symlink in PATH.
    (bin/"tinyjs").write <<~SH
      #!/bin/sh
      if [ "$1" = "update" ]; then
        echo "tinyjs is managed by Homebrew; run brew update && brew upgrade tinyjs" >&2
        exit 1
      fi
      if [ "$1" = "uninstall" ]; then
        echo "tinyjs is managed by Homebrew; run brew uninstall tinyjs" >&2
        exit 1
      fi
      exec "#{libexec}/bin/tjs" run "#{libexec}/cli.js" "$@"
    SH
  end

  def caveats
    <<~EOS
      Upgrade this installation with brew update && brew upgrade tinyjs.
      The tinyjs update command is disabled to keep Homebrew's Cellar intact.
      Remove this installation with brew uninstall tinyjs.
    EOS
  end

  test do
    assert_match "tinyjs v#{version}", shell_output("#{bin}/tinyjs version")
    assert_match "brew upgrade tinyjs", shell_output("#{bin}/tinyjs update 2>&1", 1)
    assert_match "brew uninstall tinyjs", shell_output("#{bin}/tinyjs uninstall --yes 2>&1", 1)
    system bin/"tinyjs", "new", "brew-check"
    cd "brew-check" do
      assert_path_exists "tinyjs.json"
      system bin/"tinyjs", "build"
      assert_path_exists "dist/brew-check"
      assert_path_exists "dist/brew-check.app"
      system "/usr/bin/codesign", "--verify", "--strict", "--deep", "dist/brew-check.app"
    end
  end
end
