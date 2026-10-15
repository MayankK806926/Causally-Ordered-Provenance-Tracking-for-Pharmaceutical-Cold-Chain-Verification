#!/usr/bin/env python3
"""
plot_results.py — Week 5: Generate publication-quality figures from sweep CSV.

Produces three plots from data/sweeps/results.csv:
  1. R_err vs blackout duration (naive baseline vs vector-clock at 0)
  2. F_liab vs blackout duration
  3. Storage overhead (pruned vs unbounded) over blackout durations

Usage:
  python experiments/plot_results.py [--input data/sweeps/results.csv]
                                     [--output-dir data/sweeps/]
"""

import argparse
import os
import sys

import pandas as pd
import matplotlib
matplotlib.use('Agg')  # non-interactive backend
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np


def load_data(csv_path: str) -> pd.DataFrame:
    """Load and validate the sweep CSV."""
    if not os.path.exists(csv_path):
        print(f"ERROR: CSV not found: {csv_path}", file=sys.stderr)
        sys.exit(1)

    df = pd.read_csv(csv_path)
    required = ['blackout_hours', 'r_err', 'f_liab', 's_prune',
                 'naive_inversions', 'vclock_inversions',
                 'unpruned_store', 'pruned_store']
    missing = [c for c in required if c not in df.columns]
    if missing:
        print(f"ERROR: Missing columns: {missing}", file=sys.stderr)
        sys.exit(1)

    return df


def plot_rerr(df: pd.DataFrame, output_dir: str):
    """Plot R_err vs blackout duration — reproduces report §5.2 left chart."""
    grouped = df.groupby('blackout_hours').agg(
        naive_rerr_mean=('r_err', 'mean'),
        naive_rerr_std=('r_err', 'std'),
        naive_inv_mean=('naive_inversions', 'mean'),
        vc_inv_mean=('vclock_inversions', 'mean'),
    ).reset_index()

    fig, ax = plt.subplots(figsize=(10, 6))

    # Naive baseline — error bars
    ax.errorbar(grouped['blackout_hours'], grouped['naive_rerr_mean'],
                yerr=grouped['naive_rerr_std'],
                fmt='o-', color='#e74c3c', linewidth=2, markersize=8,
                capsize=5, capthick=1.5, elinewidth=1.5,
                label='Naive Physical-Timestamp Baseline',
                zorder=3)

    # Vector clock line at zero
    ax.axhline(y=0, color='#2ecc71', linewidth=2.5, linestyle='--',
               label='Vector Clock Pipeline (guaranteed 0)', zorder=2)

    ax.set_xlabel('Mean Blackout Duration (hours)', fontsize=13)
    ax.set_ylabel('Causal Misordering Rate ($R_{err}$)', fontsize=13)
    ax.set_title('Causal Misordering Rate vs. Blackout Duration', fontsize=15, fontweight='bold')
    ax.set_xscale('log')
    ax.xaxis.set_major_formatter(ticker.ScalarFormatter())
    ax.legend(fontsize=11, loc='upper left')
    ax.grid(True, alpha=0.3)
    ax.set_ylim(bottom=-0.01)

    fig.tight_layout()
    path = os.path.join(output_dir, 'rerr_vs_blackout.png')
    fig.savefig(path, dpi=150, bbox_inches='tight')
    print(f"  Saved: {path}")
    plt.close(fig)


def plot_fliab(df: pd.DataFrame, output_dir: str):
    """Plot F_liab vs blackout duration."""
    grouped = df.groupby('blackout_hours').agg(
        fliab_mean=('f_liab', 'mean'),
        fliab_std=('f_liab', 'std'),
    ).reset_index()

    fig, ax = plt.subplots(figsize=(10, 6))

    ax.errorbar(grouped['blackout_hours'], grouped['fliab_mean'],
                yerr=grouped['fliab_std'],
                fmt='s-', color='#e67e22', linewidth=2, markersize=8,
                capsize=5, capthick=1.5, elinewidth=1.5,
                label='Naive Pipeline — False Liability Rate',
                zorder=3)

    ax.axhline(y=0, color='#2ecc71', linewidth=2.5, linestyle='--',
               label='Vector Clock Pipeline (zero misattribution)', zorder=2)

    ax.set_xlabel('Mean Blackout Duration (hours)', fontsize=13)
    ax.set_ylabel('False Liability Attribution Rate ($F_{liab}$)', fontsize=13)
    ax.set_title('False Liability Attribution vs. Blackout Duration',
                 fontsize=15, fontweight='bold')
    ax.set_xscale('log')
    ax.xaxis.set_major_formatter(ticker.ScalarFormatter())
    ax.legend(fontsize=11, loc='upper left')
    ax.grid(True, alpha=0.3)
    ax.set_ylim(bottom=-0.01)

    fig.tight_layout()
    path = os.path.join(output_dir, 'fliab_vs_blackout.png')
    fig.savefig(path, dpi=150, bbox_inches='tight')
    print(f"  Saved: {path}")
    plt.close(fig)


def plot_storage(df: pd.DataFrame, output_dir: str):
    """Plot storage reduction (S_prune) vs blackout duration."""
    grouped = df.groupby('blackout_hours').agg(
        sprune_mean=('s_prune', 'mean'),
        sprune_std=('s_prune', 'std'),
        unpruned_mean=('unpruned_store', 'mean'),
        pruned_mean=('pruned_store', 'mean'),
    ).reset_index()

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))

    # Left: S_prune ratio
    ax1.errorbar(grouped['blackout_hours'], grouped['sprune_mean'] * 100,
                 yerr=grouped['sprune_std'] * 100,
                 fmt='D-', color='#3498db', linewidth=2, markersize=8,
                 capsize=5, capthick=1.5, elinewidth=1.5,
                 label='Storage Reduction %', zorder=3)
    ax1.set_xlabel('Mean Blackout Duration (hours)', fontsize=13)
    ax1.set_ylabel('Storage Reduction ($S_{prune}$) %', fontsize=13)
    ax1.set_title('Matrix-Clock Pruning Efficiency', fontsize=15, fontweight='bold')
    ax1.set_xscale('log')
    ax1.xaxis.set_major_formatter(ticker.ScalarFormatter())
    ax1.legend(fontsize=11)
    ax1.grid(True, alpha=0.3)
    ax1.set_ylim(bottom=-5, top=105)

    # Right: Absolute counts (pruned vs unpruned)
    x = np.arange(len(grouped))
    width = 0.35
    ax2.bar(x - width/2, grouped['unpruned_mean'], width,
            color='#e74c3c', alpha=0.8, label='Without Pruning')
    ax2.bar(x + width/2, grouped['pruned_mean'], width,
            color='#2ecc71', alpha=0.8, label='After Pruning')
    ax2.set_xlabel('Mean Blackout Duration (hours)', fontsize=13)
    ax2.set_ylabel('Active State Store Records', fontsize=13)
    ax2.set_title('State Store Size: Pruned vs. Unbounded', fontsize=15, fontweight='bold')
    ax2.set_xticks(x)
    ax2.set_xticklabels([f'{h:.1f}' for h in grouped['blackout_hours']])
    ax2.legend(fontsize=11)
    ax2.grid(True, alpha=0.3, axis='y')

    fig.tight_layout()
    path = os.path.join(output_dir, 'storage_reduction.png')
    fig.savefig(path, dpi=150, bbox_inches='tight')
    print(f"  Saved: {path}")
    plt.close(fig)


def plot_inversions_bar(df: pd.DataFrame, output_dir: str):
    """Bar chart: naive inversions vs vclock inversions (always 0) per blackout."""
    grouped = df.groupby('blackout_hours').agg(
        naive_inv=('naive_inversions', 'mean'),
        vc_inv=('vclock_inversions', 'mean'),
    ).reset_index()

    fig, ax = plt.subplots(figsize=(10, 6))

    x = np.arange(len(grouped))
    width = 0.35
    ax.bar(x - width/2, grouped['naive_inv'], width,
           color='#e74c3c', alpha=0.85, label='Naive Baseline')
    ax.bar(x + width/2, grouped['vc_inv'], width,
           color='#2ecc71', alpha=0.85, label='Vector Clock (= 0)')

    ax.set_xlabel('Mean Blackout Duration (hours)', fontsize=13)
    ax.set_ylabel('Mean Causal Inversions', fontsize=13)
    ax.set_title('Causal Inversions: Vector Clock vs. Naive Baseline',
                 fontsize=15, fontweight='bold')
    ax.set_xticks(x)
    ax.set_xticklabels([f'{h:.1f}' for h in grouped['blackout_hours']])
    ax.legend(fontsize=11)
    ax.grid(True, alpha=0.3, axis='y')

    fig.tight_layout()
    path = os.path.join(output_dir, 'inversions_comparison.png')
    fig.savefig(path, dpi=150, bbox_inches='tight')
    print(f"  Saved: {path}")
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(
        description='Generate plots from cold-chain sweep results.')
    parser.add_argument('--input', default='data/sweeps/results.csv',
                        help='Path to sweep CSV (default: data/sweeps/results.csv)')
    parser.add_argument('--output-dir', default='data/sweeps/',
                        help='Directory for output PNGs (default: data/sweeps/)')
    args = parser.parse_args()

    print("=" * 72)
    print(" Cold Chain Vector Clock — Results Plotter")
    print("=" * 72)
    print(f"  Input:  {args.input}")
    print(f"  Output: {args.output_dir}")
    print()

    os.makedirs(args.output_dir, exist_ok=True)
    df = load_data(args.input)

    print(f"  Loaded {len(df)} rows, {df['blackout_hours'].nunique()} blackout settings")
    print()

    plot_rerr(df, args.output_dir)
    plot_fliab(df, args.output_dir)
    plot_storage(df, args.output_dir)
    plot_inversions_bar(df, args.output_dir)

    print()
    print("=" * 72)
    print(" All plots generated successfully.")
    print("=" * 72)


if __name__ == '__main__':
    main()
