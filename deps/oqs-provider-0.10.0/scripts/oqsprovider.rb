class Oqsprovider < Formula
  desc "OpenSSL3 provider integrating quantum safe cryptography"
  homepage "https://www.openquantumsafe.org"
  url "https://github.com/open-quantum-safe/oqs-provider/archive/refs/tags/0.5.0.tar.gz"
  sha256 "8b954eac7109084600825ab6f3a1dd861c5de043d2b6e4563ffc68406c2b20a5"
  license "MIT"
  head "https://github.com/open-quantum-safe/oqs-provider.git", branch: "main"

  depends_on "cmake" => :build
  depends_on "liboqs"
  depends_on "openssl@3"

  def install
    system "cmake", "-S", ".", "-B", "build", *std_cmake_args
    system "cmake", "--build", "build"
    with_env(CC: DevelopmentTools.locate(DevelopmentTools.default_compiler)) do
      system "ctest", "--parallel", "5", "--test-dir", "build", "--rerun-failed", "--output-on-failure"
    end
# Do not install as part of testing -- only when deployed; so comment out for now:
#    system "cmake", "--install", "build"
#    ohai "Update system openssl.cnf to activate oqsprovider by default;"
#    ohai " otherwise use 'openssl <command> -provider oqsprovider' to activate."
  end

  test do
    # This checks oqsprovider is available and executes within standard openssl installation
    output = shell_output("openssl list -providers -provider-path #{lib} -provider oqsprovider")
    assert_match("OpenSSL OQS Provider", output)
  end
end
