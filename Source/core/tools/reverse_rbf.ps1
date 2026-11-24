# Turns Quartus' .rbf into the bit-reversed .rev file the Analogue Pocket loads.
param([string]$In, [string]$Out)
Add-Type -TypeDefinition @"
public static class RbfRev {
  public static byte[] Reverse(byte[] d) {
    byte[] o = new byte[d.Length];
    for (int i = 0; i < d.Length; i++) {
      int b = d[i], r = 0;
      for (int k = 0; k < 8; k++) { r = (r << 1) | (b & 1); b >>= 1; }
      o[i] = (byte)r;
    }
    return o;
  }
}
"@
$bytes = [System.IO.File]::ReadAllBytes((Resolve-Path $In))
[System.IO.File]::WriteAllBytes($Out, [RbfRev]::Reverse($bytes))
Write-Host "Wrote $Out ($($bytes.Length) bytes)"
