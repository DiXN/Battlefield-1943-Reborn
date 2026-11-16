# The lines tools/stats_from_logs.py needs from the official servers' logs (the website's totals: players who joined a
# match, hours played), from every package folder kept on the machine. Read only: it opens the logs for reading while
# the backend keeps writing them, at below normal priority, and writes nothing on the machine.
#
#   ssh bf1943-hetzner "powershell -NoProfile -EncodedCommand <this file as UTF-16LE base64>" > hetzner.txt
#   python -I tools/stats_from_logs.py --out stats.json hetzner.txt=-8 us.txt=1
#
# The extract holds addresses and player names as the logs do: keep it on your own PC and delete it after the import
# (the stats file made from it holds counts and hashed ids only).
# On a Linux server (the US Central VPS, clock CET), the same for its host logs:
#   for f in /opt/bf1943/data/logs/server*/host.log; do printf "#FILE\t%s\t%s\t%s\n" "$f" \
#     "$(date -d @$(stat -c %W "$f") +%Y-%m-%dT%H:%M:%S)" "$(date -r "$f" +%Y-%m-%dT%H:%M:%S)"; \
#     nice grep -E "joined: game packets|left the game|dropped|listening on" "$f"; done; echo "#END"
[System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = 'BelowNormal'
$root = 'D:\TRG Studio\Servers\BF1943'
$files = Get-ChildItem -Recurse $root -Include backend.log,host.log -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notlike '*\crash-*' }
foreach ($f in $files) {
  "#FILE`t$($f.FullName)`t$($f.CreationTime.ToString('s'))`t$($f.LastWriteTime.ToString('s'))"
  $isHost = $f.Name -eq 'host.log'
  $fs = New-Object System.IO.FileStream($f.FullName, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
  $r = New-Object System.IO.StreamReader($fs)
  while ($null -ne ($l = $r.ReadLine())) {
    if ($isHost) {
      if ($l.Contains('joined: game packets') -or $l.Contains('left the game') -or $l.Contains('dropped') -or $l.Contains('listening on')) { $l }
    } elseif ($l.Contains('NuXBL360Login') -or $l.Contains('-> USER ') -or $l.Contains('-> EGAM ') -or $l.Contains(' FESL on ')) {
      if ($l.Length -gt 260) { $l.Substring(0, 260) } else { $l }
    }
  }
  $r.Close()
}
"#END"
