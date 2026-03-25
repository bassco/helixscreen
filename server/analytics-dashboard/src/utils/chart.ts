import type { ChartOptions } from 'chart.js'

export const horizontalBarOpts: ChartOptions<'bar'> = {
  indexAxis: 'y',
  scales: { y: { ticks: { autoSkip: false } } }
}

export function compareVersions(a: string, b: string): number {
  const pa = a.replace(/^v/, '').split('.').map(Number)
  const pb = b.replace(/^v/, '').split('.').map(Number)
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    const diff = (pa[i] ?? 0) - (pb[i] ?? 0)
    if (diff !== 0) return diff
  }
  return 0
}
