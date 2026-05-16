import 'package:flutter/material.dart';
import 'package:fl_chart/fl_chart.dart';
import 'package:google_fonts/google_fonts.dart';

import '../theme/app_colors.dart';
import '../data/models/trash_bin_model.dart';

class EnvironmentChart extends StatelessWidget {
  final TrashBinModel bin;

  const EnvironmentChart({super.key, required this.bin});

  @override
  Widget build(BuildContext context) {
    return BarChart(
      BarChartData(
        alignment: BarChartAlignment.spaceAround,
        maxY: 100,
        barTouchData: BarTouchData(
          enabled: true,
          touchTooltipData: BarTouchTooltipData(
            getTooltipColor: (group) => AppColors.surfaceAlt,
            tooltipRoundedRadius: 8,
            getTooltipItem: (group, groupIndex, rod, rodIndex) {
              final labels = ['Khu 1', 'Khu 2', 'Khu 3'];
              final isTemp = rodIndex == 0;
              return BarTooltipItem(
                '${labels[groupIndex]}\n',
                GoogleFonts.inter(
                  fontSize: 11,
                  color: AppColors.textSecondary,
                ),
                children: [
                  TextSpan(
                    text: isTemp
                        ? '${rod.toY.toStringAsFixed(1)}°C'
                        : '${rod.toY.toStringAsFixed(1)}%',
                    style: GoogleFonts.inter(
                      fontSize: 13,
                      fontWeight: FontWeight.w700,
                      color: AppColors.textPrimary,
                    ),
                  ),
                ],
              );
            },
          ),
        ),
        titlesData: FlTitlesData(
          show: true,
          topTitles: const AxisTitles(
              sideTitles: SideTitles(showTitles: false)),
          rightTitles: const AxisTitles(
              sideTitles: SideTitles(showTitles: false)),
          leftTitles: AxisTitles(
            sideTitles: SideTitles(
              showTitles: true,
              reservedSize: 32,
              getTitlesWidget: (value, meta) {
                if (value % 25 != 0) return const SizedBox.shrink();
                return Text(
                  '${value.toInt()}',
                  style: GoogleFonts.inter(
                    fontSize: 10,
                    color: AppColors.textMuted,
                  ),
                );
              },
            ),
          ),
          bottomTitles: AxisTitles(
            sideTitles: SideTitles(
              showTitles: true,
              getTitlesWidget: (value, meta) {
                final labels = ['Khu 1', 'Khu 2', 'Khu 3'];
                if (value.toInt() >= labels.length) {
                  return const SizedBox.shrink();
                }
                return Padding(
                  padding: const EdgeInsets.only(top: 8),
                  child: Text(
                    labels[value.toInt()],
                    style: GoogleFonts.inter(
                      fontSize: 11,
                      fontWeight: FontWeight.w500,
                      color: AppColors.textSecondary,
                    ),
                  ),
                );
              },
            ),
          ),
        ),
        gridData: FlGridData(
          show: true,
          drawVerticalLine: false,
          horizontalInterval: 25,
          getDrawingHorizontalLine: (value) => FlLine(
            color: AppColors.border,
            strokeWidth: 0.5,
          ),
        ),
        borderData: FlBorderData(show: false),
        barGroups: [
          _makeGroup(0, bin.temperature1, bin.humidity1),
          _makeGroup(1, bin.temperature2, bin.humidity2),
          _makeGroup(2, bin.temperature3, bin.humidity3),
        ],
      ),
    );
  }

  BarChartGroupData _makeGroup(int x, double temp, double hum) {
    return BarChartGroupData(
      x: x,
      barRods: [
        BarChartRodData(
          toY: temp.clamp(0, 100),
          color: AppColors.danger,
          width: 14,
          borderRadius: const BorderRadius.vertical(top: Radius.circular(4)),
        ),
        BarChartRodData(
          toY: hum.clamp(0, 100),
          color: AppColors.info,
          width: 14,
          borderRadius: const BorderRadius.vertical(top: Radius.circular(4)),
        ),
      ],
      barsSpace: 4,
    );
  }
}
