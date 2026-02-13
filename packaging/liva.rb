class Liva < Formula
  desc "Modern systems programming language with Swift-like syntax and Rust-style ownership"
  homepage "https://github.com/liva-lang/liva-lang"
  url "https://github.com/liva-lang/liva-lang/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "PLACEHOLDER_SHA256"
  license "MIT"
  head "https://github.com/liva-lang/liva-lang.git", branch: "main"

  depends_on "cmake" => :build
  depends_on "ninja" => :build
  depends_on "llvm@21" => :recommended

  def install
    args = %w[
      -DCMAKE_BUILD_TYPE=Release
    ]

    # Use Homebrew LLVM if available
    if build.with?("llvm@21")
      llvm = Formula["llvm@21"]
      args << "-DLLVM_DIR=#{llvm.opt_lib}/cmake/llvm"
    end

    system "cmake", "-G", "Ninja", "-S", ".", "-B", "build", *std_cmake_args, *args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build", "--prefix", prefix
  end

  test do
    # Test basic compilation
    (testpath/"hello.liva").write <<~LIVA
      func main() {
          let x: i32 = 42
      }
    LIVA
    system bin/"livac", "--check-only", testpath/"hello.liva"

    # Test version output
    assert_match version.to_s, shell_output("#{bin}/livac --version")
  end
end
