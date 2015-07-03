from copy import copy

import numpy as np
import matplotlib.pyplot as plt
from scipy.sparse import csr_matrix

from . import pltutils
from .utils import with_defaults
from .system import Positions, plot_sites, plot_hoppings
from .support.pickle import pickleable

__all__ = ['LDOSpoint', 'SpatialMap']


@pickleable
class LDOSpoint:
    def __init__(self, energy, ldos):
        self.energy = energy
        self.ldos = ldos

    def plot(self, **kwargs):
        plt.plot(self.energy, self.ldos, **kwargs)
        plt.xlim(self.energy.min(), self.energy.max())
        plt.ylabel('LDOS')
        plt.xlabel('E (eV)')
        pltutils.despine()


@pickleable
class SpatialMap:
    def __init__(self, data: np.ndarray, pos: Positions,
                 sublattice: np.ndarray, hoppings: csr_matrix):
        self.data = data  # 1d array of data which corresponds to (x, y, z) coordinates
        self.pos = Positions(*pos)  # make sure it is Positions
        self.sublattice = sublattice
        self.hoppings = hoppings

    @classmethod
    def from_system(cls, data, system):
        return cls(data, system.positions, system.sublattice, system.matrix.tocsr())

    def copy(self) -> 'SpatialMap':
        return copy(self)

    def save_txt(self, file_name):
        with open(file_name + '.dat', 'w') as file:
            file.write('# {:12}{:13}{:13}\n'.format('x(nm)', 'y(nm)', 'data'))
            for x, y, d in zip(self.pos.x, self.pos.y, self.data):
                file.write(("{:13.5e}" * 3 + '\n').format(x, y, d))

    def filter(self, idx):
        self.data = self.data[idx]
        self.sublattice = self.sublattice[idx]
        self.pos = Positions(*map(lambda v: v[idx], self.pos))
        self.hoppings = self.hoppings[idx][:, idx]

    def crop(self, x=None, y=None):
        xlim, ylim = x, y
        x, y, _ = self.pos
        idx = np.ones(x.size, dtype=bool)
        if xlim:
            idx = np.logical_and(idx, x >= xlim[0], x <= xlim[1])
        if ylim:
            idx = np.logical_and(idx, y >= ylim[0], y <= ylim[1])
        self.filter(idx)

    def clip(self, v_min, v_max):
        self.data = np.clip(self.data, v_min, v_max)

    def convolve(self, sigma=0.25):
        x, y, _ = self.pos
        r = np.sqrt(x**2 + y**2)

        data = np.empty_like(self.data)
        for i in range(len(data)):
            idx = np.abs(r - r[i]) < sigma
            data[i] = np.sum(self.data[idx] * np.exp(-0.5 * ((r[i] - r[idx]) / sigma)**2))
            data[i] /= np.sum(np.exp(-0.5 * ((r[i] - r[idx]) / sigma)**2))

        self.data = data

    @staticmethod
    def _decorate_plot():
        ax = plt.gca()
        ax.set_aspect('equal')
        ax.set_xlabel('x (nm)')
        ax.set_ylabel('y (nm)')
        ax.set_xticks(ax.get_xticks()[1:-1])
        ax.set_yticks(ax.get_yticks()[1:-1])

    def plot_pcolor(self, **kwargs):
        x, y, _ = self.pos
        pcolor = plt.tripcolor(x, y, self.data,
                               **with_defaults(kwargs, cmap='YlGnBu', shading='gouraud'))
        self._decorate_plot()
        return pcolor

    def plot_contourf(self, num_levels=50, **kwargs):
        levels = np.linspace(self.data.min(), self.data.max(), num=num_levels)
        x, y, _ = self.pos
        contourf = plt.tricontourf(x, y, self.data,
                                   **with_defaults(kwargs, cmap='YlGnBu', levels=levels))
        self._decorate_plot()
        return contourf

    def plot_contour(self, **kwargs):
        x, y, _ = self.pos
        contour = plt.tricontour(x, y, self.data, **kwargs)
        self._decorate_plot()
        return contour

    def plot_structure(self, site_radius=(0.03, 0.05), site_props: dict=None, hopping_width=1,
                       hopping_props: dict=None, cbar_props: dict=None):
        ax = plt.gca()
        ax.set_aspect('equal', 'datalim')
        ax.set_xlabel('x (nm)')
        ax.set_ylabel('y (nm)')

        radius = site_radius[0] + site_radius[1] * self.data / self.data.max()
        collection = plot_sites(ax, self.pos, self.data, radius,
                                **with_defaults(site_props, cmap='YlGnBu'))

        plot_hoppings(ax, self.pos, self.hoppings.tocoo(), hopping_width,
                      **with_defaults(hopping_props, colors='#bbbbbb'))

        pltutils.colorbar(collection, **with_defaults(cbar_props))
        pltutils.despine(trim=True)
        pltutils.add_margin()