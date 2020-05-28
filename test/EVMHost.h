/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * EVM execution host, i.e. component that implements a simulated Ethereum blockchain
 * for testing purposes.
 */

#pragma once

#include <test/evmc/mocked_host.hpp>
#include <test/evmc/evmc.hpp>
#include <test/evmc/evmc.h>

#include <liblangutil/EVMVersion.h>

#include <libsolutil/FixedHash.h>

#include <boost/filesystem/path.hpp>

namespace solidity::test
{
using Address = util::h160;

class EVMHost: public evmc::MockedHost
{
public:
	using MockedHost::get_balance;
	using MockedHost::get_code_size;

	/// Tries to dynamically load libevmone. @returns nullptr on failure.
	/// The path has to be provided for the first successful run and will be ignored
	/// afterwards.
	static evmc::VM& getVM(std::string _path = {});

	explicit EVMHost(langutil::EVMVersion _evmVersion, evmc::VM& _vm = getVM());

	virtual void reset()
	{
		accounts.clear();
		m_currentAddress = {};
	}
	virtual void newBlock()
	{
		tx_context.block_number++;
		tx_context.block_timestamp += 15;
		recorded_logs.clear();
	}

	bool account_exists(evmc::address const& _addr) const noexcept override
	{
		return evmc::MockedHost::account_exists(_addr);
	}

	void selfdestruct(evmc::address const& _addr, evmc::address const& _beneficiary) noexcept override;

	evmc::result call(evmc_message const& _message) noexcept override;

	evmc::bytes32 get_block_hash(int64_t number) const noexcept override;

	static Address convertFromEVMC(evmc::address const& _addr);
	static evmc::address convertToEVMC(Address const& _addr);
	static util::h256 convertFromEVMC(evmc::bytes32 const& _data);
	static evmc::bytes32 convertToEVMC(util::h256 const& _data);

private:
	evmc::address m_currentAddress = {};

	static evmc::result precompileECRecover(evmc_message const& _message) noexcept;
	static evmc::result precompileSha256(evmc_message const& _message) noexcept;
	static evmc::result precompileRipeMD160(evmc_message const& _message) noexcept;
	static evmc::result precompileIdentity(evmc_message const& _message) noexcept;
	static evmc::result precompileModExp(evmc_message const& _message) noexcept;
	static evmc::result precompileALTBN128G1Add(evmc_message const& _message) noexcept;
	static evmc::result precompileALTBN128G1Mul(evmc_message const& _message) noexcept;
	static evmc::result precompileALTBN128PairingProduct(evmc_message const& _message) noexcept;
	static evmc::result precompileGeneric(evmc_message const& _message, std::map<bytes, bytes> const& _inOut) noexcept;
	/// @returns a result object with no gas usage and result data taken from @a _data.
	/// @note The return value is only valid as long as @a _data is alive!
	static evmc::result resultWithGas(evmc_message const& _message, bytes const& _data) noexcept;

	evmc::VM& m_vm;
	// EVM version requested by the testing tool
	langutil::EVMVersion m_evmVersion;
	// EVM version requested from EVMC (matches the above)
	evmc_revision m_evmRevision;
};

class EVMHosts: public EVMHost
{
public:
	explicit EVMHosts(langutil::EVMVersion _evmVersion, std::vector<boost::filesystem::path> const& _paths)
		: EVMHost(_evmVersion)
	{
		for (auto const& path: _paths)
		{
			evmc::VM& vm = EVMHost::getVM(path.string());
			if (vm)
				m_evmHosts.emplace_back(std::make_shared<EVMHost>(_evmVersion, vm));
		}
	}

	static bool LibrariesReady(std::vector<boost::filesystem::path> const& _paths)
	{
		bool success = true;
		for (auto const& path: _paths)
			if (!solidity::test::EVMHost::getVM(path.string()))
				success = false;
		return success && !_paths.empty();
	}

	void reset() override
	{
		for (auto& host: m_evmHosts)
			host->reset();
	}

	void newBlock() override
	{
		for (auto& host: m_evmHosts)
			host->newBlock();
	}

	bool account_exists(evmc::address const& _addr) const noexcept override
	{
		bool result = true;
		for (auto& host: m_evmHosts)
			result &= host->account_exists(_addr);
		return result;
	}

	void selfdestruct(evmc::address const& _addr, evmc::address const& _beneficiary) noexcept override
	{
		for (auto& host: m_evmHosts)
			host->selfdestruct(_addr, _beneficiary);
	}

	evmc::result call(evmc_message const& _message) noexcept override
	{
		bool error = false;
		std::vector<evmc::result> results;

		for (auto& host: m_evmHosts)
			results.push_back(host->call(_message));

		// check results
		for (auto& result: results)
			if (result.status_code != results.begin()->status_code)
				error = true;

		if (!error)
			return evmc::result(std::move(*results.begin()));

		return evmc::result(evmc_status_code::EVMC_INTERNAL_ERROR, 0, nullptr, 0);
	}

	evmc::bytes32 get_block_hash(int64_t number) const noexcept override { return EVMHost::get_block_hash(number); }

private:
	std::vector<std::shared_ptr<EVMHost>> m_evmHosts;
};

}
